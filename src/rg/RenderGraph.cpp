#include "myvk_rg/RenderGraph.hpp"
#include "myvk_rg/_details_/RenderGraphBase.hpp"

#include <iostream>

namespace myvk_rg::_details_ {

class RenderGraphImage final : public myvk::ImageBase {
private:
	const RenderGraphBase *m_render_graph_ptr;
	uint32_t m_image_id;

public:
	inline RenderGraphImage(const RenderGraphBase *render_graph_ptr, uint32_t image_id)
	    : m_render_graph_ptr{render_graph_ptr}, m_image_id{image_id} {
		const auto &image_info = render_graph_ptr->m_compile_info.internal_images[image_id];
		m_image = image_info.vk_image;
		// TODO: Fill the data
		// m_size = image_info.image->GetSize();
	}
	inline ~RenderGraphImage() final = default;
	const myvk::Ptr<myvk::Device> &GetDevicePtr() const final { return m_render_graph_ptr->GetDevicePtr(); }
};
class RenderGraphBuffer final : public myvk::BufferBase {
private:
	const RenderGraphBase *m_render_graph_ptr;
	uint32_t m_buffer_id;

public:
	inline RenderGraphBuffer(const RenderGraphBase *render_graph_ptr, uint32_t buffer_id)
	    : m_render_graph_ptr{render_graph_ptr}, m_buffer_id{buffer_id} {
		const auto &buffer_info = render_graph_ptr->m_compile_info.internal_buffers[buffer_id];
		m_buffer = buffer_info.vk_buffer;
		m_size = buffer_info.buffer->GetSize();
	}
	inline ~RenderGraphBuffer() final = default;
	const myvk::Ptr<myvk::Device> &GetDevicePtr() const final { return m_render_graph_ptr->GetDevicePtr(); }
};

void RenderGraphBase::_visit_resource_dep_passes(const ResourceBase *resource) const {
	// Mark Visited Pass, Generate Temporal Managed Resource Sets
	const auto visit_dep_pass = [this](const PassBase *dep_pass) -> void {
		if (dep_pass && !dep_pass->m_internal_info.visited) {
			dep_pass->m_internal_info.visited = true;
			// Further Traverse dep_pass's dependent Resources
			dep_pass->for_each_input(
			    [this](const Input *p_input) { _visit_resource_dep_passes(p_input->GetResource()); });
		}
	};
	resource->Visit([this, &visit_dep_pass](auto *resource) -> void {
		constexpr auto kClass = ResourceVisitorTrait<decltype(resource)>::kClass;
		// For CombinedImage, further For Each its Child Images
		if constexpr (kClass == ResourceClass::kCombinedImage) {
			if (resource->m_internal_info._has_parent_ == false)
				m_compile_info._internal_image_set_.insert(resource);
			// Visit Each SubImage
			resource->ForEachImage([&visit_dep_pass](auto *sub_image) -> void {
				if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kIsCombinedOrManagedImage) {
					sub_image->m_internal_info._has_parent_ = true;
				} else {
					// Is ImageAlias
					sub_image->GetPointedResource()->Visit([](auto *sub_image) -> void {
						if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kIsCombinedOrManagedImage)
							sub_image->m_internal_info._has_parent_ = true;
					});
					visit_dep_pass(sub_image->GetProducerPass());
				}
			});
		} else {
			if constexpr (kClass == ResourceClass::kManagedImage) {
				if (resource->m_internal_info._has_parent_ == false)
					m_compile_info._internal_image_set_.insert(resource);
			} else if constexpr (kClass == ResourceClass::kManagedBuffer) {
				m_compile_info._internal_buffer_set_.insert(resource);
			} else if constexpr (GetResourceState(kClass) == ResourceState::kAlias)
				visit_dep_pass(resource->GetProducerPass());
		}
	});
}
void RenderGraphBase::_extract_visited_passes(const std::vector<PassBase *> *p_cur_seq) const {
	for (const auto pass : *p_cur_seq) {
		if (pass->m_internal_info.visited) {
			pass->m_internal_info.visited = false; // Restore
			pass->m_internal_info.id = m_compile_info.passes.size();
			m_compile_info.passes.push_back({pass});
		} else if (pass->m_p_pass_pool_sequence)
			_extract_visited_passes(pass->m_p_pass_pool_sequence);
	}
}

void RenderGraphBase::_initialize_combined_image(const CombinedImage *image) {
	// Visit Each Child Image, Update Size and Base Layer
	auto *parent = image->m_internal_info.parent ? image->m_internal_info.parent : image;
	image->ForEachExpandedImage([image, parent](auto *sub_image) -> void {
		if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kIsCombinedOrManagedImage) {
			sub_image->m_internal_info.image_id = image->m_internal_info.image_id;
			sub_image->m_internal_info.parent = parent;
			sub_image->m_internal_info._has_parent_ = false; // Restore _has_parent_

			// Merge the Size of the Current Child Image
			if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kClass == ResourceClass::kCombinedImage)
				_initialize_combined_image(sub_image); // Further Query SubImage Size
		}
	});
}

void RenderGraphBase::assign_pass_resource_indices() const {
	_destroy_vk_resource();
	{ // Generate Pass Sequence
		m_compile_info.passes.clear();
		for (auto it = m_p_result_pool_data->pool.begin(); it != m_p_result_pool_data->pool.end(); ++it)
			_visit_resource_dep_passes(*m_p_result_pool_data->ValueGet<0, ResourceBase *>(it));
		_extract_visited_passes(m_p_pass_pool_sequence);
	}

	{ // Clean and Generate Internal Buffer List
		m_compile_info.internal_buffers.clear();
		m_compile_info.internal_buffers.reserve(m_compile_info._internal_buffer_set_.size());
		for (auto *buffer : m_compile_info._internal_buffer_set_) {
			buffer->m_internal_info.buffer_id = m_compile_info.internal_buffers.size();
			m_compile_info.internal_buffers.emplace_back();
			m_compile_info.internal_buffers.back().buffer = buffer;
		}
		m_compile_info._internal_buffer_set_.clear();
	}

	{ // Clean and Generate Internal Image List
		m_compile_info.internal_images.clear();
		// Add all top-level ManagedImages to the ManagedImage List
		for (auto *image : m_compile_info._internal_image_set_) {
			image->Visit([this](auto *image) -> void {
				if constexpr (ResourceVisitorTrait<decltype(image)>::kIsCombinedOrManagedImage) {
					if (!image->m_internal_info._has_parent_) {
						image->m_internal_info.image_id = m_compile_info.internal_images.size();
						m_compile_info.internal_images.emplace_back();
						m_compile_info.internal_images.back().image = image;
					}
				}
			});
		}
		m_compile_info._internal_image_set_.clear();
		// Initialize Combined Image
		for (auto &image_info : m_compile_info.internal_images) {
			image_info.image->Visit([](auto *image) -> void {
				if constexpr (ResourceVisitorTrait<decltype(image)>::kClass == ResourceClass::kCombinedImage) {
					_initialize_combined_image(image);
				}
			});
		}
	}

	printf("managed image count: %ld\n", m_compile_info.internal_images.size());
	printf("managed buffer count: %ld\n", m_compile_info.internal_buffers.size());
}

void RenderGraphBase::_compute_merge_length() const {
	// Calculate _merge_length_, Complexity: O(N + M)
	// _merge_length_ == 0: The pass is not a graphics pass
	// _merge_length_ == 1: The pass is a graphics pass, but can't be merged
	// _merge_length_ >  1: The pass is a graphics pass, and it can be merged to a group of _merge_length_ with the
	// passes before
	{
		m_compile_info.passes[0]._merge_length_ = m_compile_info.passes[0].pass->m_p_attachment_data ? 1u : 0u;
		for (uint32_t i = 1; i < m_compile_info.passes.size(); ++i) {
			auto &cur_pass_info = m_compile_info.passes[i];
			const auto &prev_pass_info = m_compile_info.passes[i - 1];
			cur_pass_info._merge_length_ =
			    cur_pass_info.pass->m_p_attachment_data ? prev_pass_info._merge_length_ + 1 : 0;
		}
	}
	for (auto &pass_info : m_compile_info.passes) {
		auto &length = pass_info._merge_length_;
		if (length <= 1)
			continue;
		uint32_t pass_id = pass_info.pass->m_internal_info.id;
		pass_info.pass->for_each_input([pass_id, &length](const Input *p_input) {
			if (!UsageIsAttachment(p_input->GetUsage())) {
				// If an input is not attachment, then all its producers can't be merged
				p_input->GetResource()->Visit([pass_id, &length](auto *resource) -> void {
					// For CombinedImage, further For Each its Child Images
					if constexpr (ResourceVisitorTrait<decltype(resource)>::kClass == ResourceClass::kCombinedImage) {
						resource->ForEachImage([pass_id, &length](auto *sub_image) -> void {
							if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kIsAlias) {
								if (sub_image->GetProducerPass())
									length =
									    std::min(length, pass_id - sub_image->GetProducerPass()->m_internal_info.id);
							}
						});
					} else if constexpr (ResourceVisitorTrait<decltype(resource)>::kIsAlias) {
						if (resource->GetProducerPass())
							length = std::min(length, pass_id - resource->GetProducerPass()->m_internal_info.id);
					}
				});
			} else {
				// If an input is attachment, but it is not produced as an attachment, then the producer can't be merged
				p_input->GetResource()->Visit([pass_id, &length](auto *resource) -> void {
					// For CombinedImage, further For Each its Child Images
					if constexpr (ResourceVisitorTrait<decltype(resource)>::kClass == ResourceClass::kCombinedImage) {
						resource->ForEachImage([pass_id, &length](auto *sub_image) -> void {
							if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kIsAlias) {
								if (sub_image->GetProducerPass() &&
								    !UsageIsAttachment(sub_image->GetProducerInput()->GetUsage()))
									length =
									    std::min(length, pass_id - sub_image->GetProducerPass()->m_internal_info.id);
							}
						});
					} else if constexpr (ResourceVisitorTrait<decltype(resource)>::kIsAlias) {
						if (resource->GetProducerPass() && !UsageIsAttachment(resource->GetProducerInput()->GetUsage()))
							length = std::min(length, pass_id - resource->GetProducerPass()->m_internal_info.id);
					}
				});
			}
		});
	}
}
inline void UpdateVkImageTypeFromVkImageViewType(VkImageType *p_image_type, VkImageViewType view_type) {
	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkImageViewCreateInfo.html
	switch (view_type) {
	case VK_IMAGE_VIEW_TYPE_1D:
	case VK_IMAGE_VIEW_TYPE_1D_ARRAY:
		*p_image_type = VK_IMAGE_TYPE_1D;
		return;
	case VK_IMAGE_VIEW_TYPE_CUBE:
	case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:
		*p_image_type = VK_IMAGE_TYPE_2D;
		return;
	case VK_IMAGE_VIEW_TYPE_2D:
	case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
		if (*p_image_type == VK_IMAGE_TYPE_1D)
			*p_image_type = VK_IMAGE_TYPE_2D;
		return;
	case VK_IMAGE_VIEW_TYPE_3D:
		*p_image_type = VK_IMAGE_TYPE_3D;
		return;
	default:
		return;
	}
}
void RenderGraphBase::_compute_resource_property_and_lifespan() const {
	// Initialize First & Last Pass Indices, Generate CombinedImage Data
	for (auto &buffer_info : m_compile_info.internal_buffers) {
		buffer_info.first_pass = buffer_info.last_pass = -1;
		buffer_info.vk_buffer_usages = 0;
	}
	for (auto &image_info : m_compile_info.internal_images) {
		image_info.first_pass = image_info.last_pass = -1;
		image_info.vk_image_usages = 0;
		image_info.vk_image_type = VK_IMAGE_TYPE_2D;
	}
	// Calculate Internal Resource Pass Range: first_pass, last_pass, Resolve Resource Usage
	// (VkImageUsageFlags and VkBufferUsageFlags)
	for (uint32_t i = 0; i < m_compile_info.passes.size(); ++i) {
		const auto &pass_info = m_compile_info.passes[i];
		uint32_t cur_first_pass = i, cur_last_pass = i;
		if (~pass_info.render_pass_id) {
			cur_first_pass = m_compile_info.render_passes[pass_info.render_pass_id].first_pass;
			cur_last_pass = m_compile_info.render_passes[pass_info.render_pass_id].last_pass;
		}
		const auto update_pass_range = [this, cur_first_pass, cur_last_pass](const auto *internal_resource) -> void {
			if constexpr (ResourceVisitorTrait<decltype(internal_resource)>::kIsInternal) {
				if constexpr (ResourceVisitorTrait<decltype(internal_resource)>::kType == ResourceType::kImage) {
					auto &image_info = m_compile_info.internal_images[internal_resource->m_internal_info.image_id];
					uint32_t &image_first_pass = image_info.first_pass, &image_last_pass = image_info.last_pass;
					if (~image_first_pass) {
						image_first_pass = std::min(image_first_pass, cur_first_pass);
						image_last_pass = std::max(image_last_pass, cur_last_pass);
					} else
						std::tie(image_first_pass, image_last_pass) = std::tie(cur_first_pass, cur_last_pass);
				} else {
					auto &buffer_info = m_compile_info.internal_buffers[internal_resource->m_internal_info.buffer_id];
					uint32_t &buffer_first_pass = buffer_info.first_pass, &buffer_last_pass = buffer_info.last_pass;
					if (~buffer_first_pass) {
						buffer_first_pass = std::min(buffer_first_pass, cur_first_pass);
						buffer_last_pass = std::max(buffer_last_pass, cur_last_pass);
					} else
						std::tie(buffer_first_pass, buffer_last_pass) = std::tie(cur_first_pass, cur_last_pass);
				}
			}
		};
		const auto update_creation_info = [this](const auto *internal_resource, const Input *p_input) -> void {
			if constexpr (ResourceVisitorTrait<decltype(internal_resource)>::kIsInternal) {
				if constexpr (ResourceVisitorTrait<decltype(internal_resource)>::kType == ResourceType::kImage) {
					auto &image_info = m_compile_info.internal_images[internal_resource->m_internal_info.image_id];
					image_info.vk_image_usages |= UsageGetCreationUsages(p_input->GetUsage());
					UpdateVkImageTypeFromVkImageViewType(&image_info.vk_image_type, internal_resource->GetViewType());
				} else {
					auto &buffer_info = m_compile_info.internal_buffers[internal_resource->m_internal_info.buffer_id];
					buffer_info.vk_buffer_usages |= UsageGetCreationUsages(p_input->GetUsage());
				}
			}
		};
		const auto set_persistent_pass_range = [this](const auto *internal_resource) -> void {
			if constexpr (ResourceVisitorTrait<decltype(internal_resource)>::kIsInternal) {
				if constexpr (ResourceVisitorTrait<decltype(internal_resource)>::kType == ResourceType::kImage) {
					uint32_t index = internal_resource->m_internal_info.image_id;
					m_compile_info.internal_images[index].first_pass = 0;
					m_compile_info.internal_images[index].last_pass = m_compile_info.passes.size() - 1;
				} else {
					uint32_t index = internal_resource->m_internal_info.buffer_id;
					m_compile_info.internal_buffers[index].first_pass = 0;
					m_compile_info.internal_buffers[index].last_pass = m_compile_info.passes.size() - 1;
				}
			}
		};
		pass_info.pass->for_each_input(
		    [&update_pass_range, &update_creation_info, &set_persistent_pass_range](const Input *p_input) -> void {
			    p_input->GetResource()->Visit([&update_pass_range, &update_creation_info, &set_persistent_pass_range,
			                                   p_input](const auto *resource) -> void {
				    if constexpr (ResourceVisitorTrait<decltype(resource)>::kIsAlias) {
					    if (resource->GetProducerPass())
						    resource->GetPointedResource()->Visit(update_pass_range);
					    else // An Alias with NULL ProducerPass indicates an Input from previous frame
						    resource->GetPointedResource()->Visit(set_persistent_pass_range);
					    resource->GetPointedResource()->Visit([&update_creation_info, p_input](const auto *resource) {
						    return update_creation_info(resource, p_input);
					    });
				    } else if constexpr (ResourceVisitorTrait<decltype(resource)>::kIsInternal) {
					    update_pass_range(resource);
					    update_creation_info(resource, p_input);
				    }
			    });
		    });
	}
	// Test Transient Image
	for (auto &image_info : m_compile_info.internal_images) {
		uint32_t first_render_pass = m_compile_info.passes[image_info.first_pass].render_pass_id;
		uint32_t last_render_pass = m_compile_info.passes[image_info.last_pass].render_pass_id;
		// If the image is used inside a single RenderPass, then it can be transient
		image_info.is_transient = (~first_render_pass) && first_render_pass == last_render_pass;
	}
}
void RenderGraphBase::merge_subpass() const {
	_compute_merge_length();
	{ // Assign Render Pass Indices
		m_compile_info.render_passes.clear();

		for (uint32_t i = 0u, prev_length = 0u; i < m_compile_info.passes.size(); ++i) {
			auto &pass_info = m_compile_info.passes[i];
			auto &length = pass_info._merge_length_;
			if (length > prev_length)
				length = prev_length + 1;

			if (length == 0)
				pass_info.render_pass_id = -1;
			else if (length == 1) {
				pass_info.render_pass_id = m_compile_info.render_passes.size();
				m_compile_info.render_passes.emplace_back();
				auto &render_pass = m_compile_info.render_passes.back();
				render_pass.first_pass = render_pass.last_pass = i;
			} else if (length > 1) {
				pass_info.render_pass_id = m_compile_info.render_passes.size() - 1;
				auto &render_pass = m_compile_info.render_passes.back();
				render_pass.last_pass = i;
			}

			prev_length = length;
		}
	}
	_compute_resource_property_and_lifespan();

	for (const auto &pass_info : m_compile_info.passes) {
		auto pass = pass_info.pass;
		std::cout << pass->GetKey().GetName() << ":" << pass->GetKey().GetID() << ".id = " << pass->m_internal_info.id
		          << " .merge_len = " << pass_info._merge_length_ << " .render_pass_id = " << pass_info.render_pass_id
		          << std::endl;
	}
	for (const auto &render_pass_info : m_compile_info.render_passes) {
		std::cout << "first_pass = " << render_pass_info.first_pass << " last_pass = " << render_pass_info.last_pass
		          << std::endl;
	}
}

void RenderGraphBase::_maintain_combined_image_size(const CombinedImage *image) {
	// Visit Each Child Image, Update Size and Base Layer
	image->m_internal_info.size = {};
	image->ForEachExpandedImage([image](auto *sub_image) -> void {
		if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kIsCombinedOrManagedImage) {
			sub_image->m_internal_info.base_layer =
			    image->m_internal_info.base_layer + image->m_internal_info.size.GetArrayLayers();

			// Merge the Size of the Current Child Image
			if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kClass == ResourceClass::kManagedImage) {
				image->m_internal_info.size.Merge(sub_image->GetSize());
			} else if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kClass == ResourceClass::kCombinedImage) {
				_maintain_combined_image_size(sub_image); // Further Query SubImage Size
				image->m_internal_info.size.Merge(sub_image->m_internal_info.size);
			}
		}
	});
}

void RenderGraphBase::_destroy_vk_resource() const {
	for (const auto &buffer_info : m_compile_info.internal_buffers) {
		if (buffer_info.vk_buffer != VK_NULL_HANDLE)
			vkDestroyBuffer(GetDevicePtr()->GetHandle(), buffer_info.vk_buffer, nullptr);
	}
	for (const auto &image_info : m_compile_info.internal_images) {
		if (image_info.vk_image != VK_NULL_HANDLE)
			vkDestroyImage(GetDevicePtr()->GetHandle(), image_info.vk_image, nullptr);
	}
}

void RenderGraphBase::_create_vk_resource() const {
	// Query Resource Size
	for (auto &buffer_info : m_compile_info.internal_buffers) {
		VkBufferCreateInfo create_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
		create_info.usage = buffer_info.vk_buffer_usages;
		create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		create_info.size = buffer_info.buffer->GetSize();
		vkCreateBuffer(GetDevicePtr()->GetHandle(), &create_info, nullptr, &buffer_info.vk_buffer);
		vkGetBufferMemoryRequirements(GetDevicePtr()->GetHandle(), buffer_info.vk_buffer,
		                              &buffer_info.vk_memory_requirements);
	}
	for (auto &image_info : m_compile_info.internal_images) {
		const SubImageSize &image_size = image_info.image->Visit([](auto *image) -> const SubImageSize & {
			if constexpr (ResourceVisitorTrait<decltype(image)>::kIsInternal) {
				image->m_internal_info.parent = nullptr;
				image->m_internal_info.base_layer = 0;
				if constexpr (ResourceVisitorTrait<decltype(image)>::kClass == ResourceClass::kCombinedImage) {
					_maintain_combined_image_size(image);
					return image->m_internal_info.size;
				} else
					return image->GetSize();
			} else {
				assert(false);
				return {};
			}
		});
		assert(image_size.GetBaseMipLevel() == 0);
		VkImageCreateInfo create_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
		create_info.usage = image_info.vk_image_usages;
		if (image_info.is_transient)
			create_info.usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
		create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		create_info.format = image_info.image->GetFormat();
		create_info.samples = VK_SAMPLE_COUNT_1_BIT;
		create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
		create_info.imageType = image_info.vk_image_type;
		{ // Set Size Info
			VkExtent3D &extent = create_info.extent;
			extent = {1, 1, 1};
			switch (create_info.imageType) {
			case VK_IMAGE_TYPE_1D: {
				extent.width = image_size.GetExtent().width;
				create_info.mipLevels = image_size.GetMipLevels();
				create_info.arrayLayers = image_size.GetArrayLayers();
			} break;
			case VK_IMAGE_TYPE_2D: {
				extent.width = image_size.GetExtent().width;
				extent.height = image_size.GetExtent().height;
				create_info.mipLevels = image_size.GetMipLevels();
				create_info.arrayLayers = image_size.GetArrayLayers();
			} break;
			case VK_IMAGE_TYPE_3D: {
				assert(image_size.GetExtent().depth == 1 || image_size.GetArrayLayers() == 1);
				extent.width = image_size.GetExtent().width;
				extent.height = image_size.GetExtent().height;
				extent.depth = std::max(image_size.GetExtent().depth, image_size.GetArrayLayers());
				create_info.mipLevels = image_size.GetMipLevels();
				create_info.arrayLayers = 1;
			} break;
			default:
				assert(false);
			}
		}
		vkCreateImage(GetDevicePtr()->GetHandle(), &create_info, nullptr, &image_info.vk_image);
		vkGetImageMemoryRequirements(GetDevicePtr()->GetHandle(), image_info.vk_image,
		                             &image_info.vk_memory_requirements);
	}
}
void RenderGraphBase::generate_vk_resource() const {
	_destroy_vk_resource();
	_create_vk_resource();
	for (const auto &image_info : m_compile_info.internal_images) {
		auto image = image_info.image;
		std::cout << image->GetKey().GetName() << ":" << image->GetKey().GetID()
		          << " mip_levels = " << image->Visit([](auto *image) -> uint32_t {
			             if constexpr (ResourceVisitorTrait<decltype(image)>::kClass == ResourceClass::kCombinedImage)
				             return image->m_internal_info.size.GetMipLevels();
			             else if constexpr (ResourceVisitorTrait<decltype(image)>::kClass ==
			                                ResourceClass::kManagedImage)
				             return image->GetSize().GetMipLevels();
			             return 0;
		             })
		          << " [" << image_info.first_pass << ", " << image_info.last_pass << "]"
		          << " usage = " << image_info.vk_image_usages << " {size, alignment, flag} = {"
		          << image_info.vk_memory_requirements.size << ", " << image_info.vk_memory_requirements.alignment
		          << ", " << image_info.vk_memory_requirements.memoryTypeBits << "}" << std::endl;
	}

	for (const auto &buffer_info : m_compile_info.internal_buffers) {
		auto buffer = buffer_info.buffer;
		std::cout << buffer->GetKey().GetName() << ":" << buffer->GetKey().GetID() << " [" << buffer_info.first_pass
		          << ", " << buffer_info.last_pass << "]"
		          << " {size, alignment, flag} = {" << buffer_info.vk_memory_requirements.size << ", "
		          << buffer_info.vk_memory_requirements.alignment << ", "
		          << buffer_info.vk_memory_requirements.memoryTypeBits << "}" << std::endl;
	}
}

inline constexpr VkShaderStageFlags VkShaderStagesFromVkPipelineStages(VkPipelineStageFlags2 pipeline_stages) {
	VkShaderStageFlags ret = 0;
	if (pipeline_stages & VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT)
		ret |= VK_SHADER_STAGE_VERTEX_BIT;
	if (pipeline_stages & VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT)
		ret |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
	if (pipeline_stages & VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT)
		ret |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
	if (pipeline_stages & VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT)
		ret |= VK_SHADER_STAGE_GEOMETRY_BIT;
	if (pipeline_stages & VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT)
		ret |= VK_SHADER_STAGE_FRAGMENT_BIT;
	if (pipeline_stages & VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
		ret |= VK_SHADER_STAGE_COMPUTE_BIT;
	return ret;
}

const myvk::Ptr<myvk::DescriptorSetLayout> &
DescriptorSetData::GetVkDescriptorSetLayout(const myvk::Ptr<myvk::Device> &device) const {
	if (m_modified) {
		if (m_bindings.empty()) {
			m_descriptor_set_layout = nullptr;
		} else {
			std::vector<VkDescriptorSetLayoutBinding> bindings;
			std::vector<VkSampler> immutable_samplers;
			bindings.reserve(m_bindings.size());
			immutable_samplers.reserve(m_bindings.size());

			for (const auto &binding_data : m_bindings) {
				bindings.emplace_back();
				VkDescriptorSetLayoutBinding &info = bindings.back();
				info.binding = binding_data.first;
				info.descriptorType = UsageGetDescriptorType(binding_data.second.GetInputPtr()->GetUsage());
				info.descriptorCount = 1;
				info.stageFlags =
				    VkShaderStagesFromVkPipelineStages(binding_data.second.GetInputPtr()->GetUsagePipelineStages());
				if (info.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
				    binding_data.second.GetVkSampler()) {
					immutable_samplers.push_back(binding_data.second.GetVkSampler()->GetHandle());
					info.pImmutableSamplers = &immutable_samplers.back();
				}
			}
			m_descriptor_set_layout = myvk::DescriptorSetLayout::Create(device, bindings);
		}
		m_modified = false;
	}
	return m_descriptor_set_layout;
}

void RenderGraphBase::SetCanvasSize(const VkExtent2D &canvas_size) {
	if (canvas_size.width != m_canvas_size.width || canvas_size.height != m_canvas_size.height) {
		m_canvas_size = canvas_size;
		set_compile_phrase(CompilePhrase::kGenerateVkResource);
	}
}

} // namespace myvk_rg::_details_
