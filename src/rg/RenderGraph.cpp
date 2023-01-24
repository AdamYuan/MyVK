#include "myvk_rg/RenderGraph.hpp"
#include "myvk_rg/_details_/RenderGraphBase.hpp"

#include <algorithm>
#include <iostream>
#include <queue>

namespace myvk_rg::_details_ {

class RenderGraphImage final : public myvk::ImageBase {
private:
	const RenderGraphBase *m_render_graph_ptr;

public:
	inline RenderGraphImage(const RenderGraphBase *render_graph_ptr, const VkImageCreateInfo &create_info)
	    : m_render_graph_ptr{render_graph_ptr} {
		vkCreateImage(GetDevicePtr()->GetHandle(), &create_info, nullptr, &m_image);
		m_extent = create_info.extent;
		m_mip_levels = create_info.mipLevels;
		m_array_layers = create_info.arrayLayers;
		m_format = create_info.format;
		m_type = create_info.imageType;
		m_usage = create_info.usage;
	}
	inline ~RenderGraphImage() final {
		if (m_image != VK_NULL_HANDLE)
			vkDestroyImage(GetDevicePtr()->GetHandle(), m_image, nullptr);
	};
	const myvk::Ptr<myvk::Device> &GetDevicePtr() const final { return m_render_graph_ptr->GetDevicePtr(); }
};
class RenderGraphBuffer final : public myvk::BufferBase {
private:
	const RenderGraphBase *m_render_graph_ptr;

public:
	inline RenderGraphBuffer(const RenderGraphBase *render_graph_ptr, const VkBufferCreateInfo &create_info)
	    : m_render_graph_ptr{render_graph_ptr} {
		vkCreateBuffer(GetDevicePtr()->GetHandle(), &create_info, nullptr, &m_buffer);
		m_size = create_info.size;
	}
	inline ~RenderGraphBuffer() final {
		if (m_buffer != VK_NULL_HANDLE)
			vkDestroyBuffer(GetDevicePtr()->GetHandle(), m_buffer, nullptr);
	}
	const myvk::Ptr<myvk::Device> &GetDevicePtr() const final { return m_render_graph_ptr->GetDevicePtr(); }
};

class RenderGraphAllocation final : public myvk::DeviceObjectBase {
private:
	const RenderGraphBase *m_render_graph_ptr;
	VmaAllocation m_allocation{VK_NULL_HANDLE};

public:
	inline RenderGraphAllocation(const RenderGraphBase *render_graph_ptr,
	                             const VkMemoryRequirements &memory_requirements,
	                             const VmaAllocationCreateInfo &create_info)
	    : m_render_graph_ptr{render_graph_ptr} {
		vmaAllocateMemory(GetDevicePtr()->GetAllocatorHandle(), &memory_requirements, &create_info, &m_allocation,
		                  nullptr);
	}
	inline ~RenderGraphAllocation() final {
		if (m_allocation != VK_NULL_HANDLE)
			vmaFreeMemory(GetDevicePtr()->GetAllocatorHandle(), m_allocation);
	}
	inline VmaAllocation GetHandle() const { return m_allocation; }
	const myvk::Ptr<myvk::Device> &GetDevicePtr() const final { return m_render_graph_ptr->GetDevicePtr(); }
};

void RenderGraphBase::_visit_resource_dep_passes(const ResourceBase *resource, const PassBase *pass,
                                                 Usage usage) const {
	// Mark Visited Pass, Generate Temporal Managed Resource Sets
	const auto add_edge = [this, pass, usage](const ResourceBase *resource, const PassBase *dep_pass) -> void {
		if (pass)
			_pass_graph_add_edge(dep_pass, pass, resource, usage);
	};
	const auto add_edge_and_visit_dep_pass = [this, &add_edge](const ResourceBase *resource,
	                                                           const PassBase *dep_pass) -> void {
		bool not_visited = m_compile_info._pass_graph_.find(dep_pass) == m_compile_info._pass_graph_.end();
		add_edge(resource, dep_pass);
		if (dep_pass && not_visited) {
			// Further Traverse dep_pass's dependent Resources
			dep_pass->for_each_input([this, dep_pass](const Input *p_input) {
				_visit_resource_dep_passes(p_input->GetResource(), dep_pass, p_input->GetUsage());
			});
		}
	};
	resource->Visit([this, &add_edge_and_visit_dep_pass, &add_edge](auto *resource) -> void {
		constexpr auto kClass = ResourceVisitorTrait<decltype(resource)>::kClass;
		// For CombinedImage, further For Each its Child Images
		if constexpr (kClass == ResourceClass::kCombinedImage) {
			m_compile_info._internal_image_set_.insert(resource);
			// Visit Each SubImage
			resource->ForEachImage([&add_edge_and_visit_dep_pass, &add_edge](auto *sub_image) -> void {
				if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kClass == ResourceClass::kManagedImage) {
					sub_image->m_internal_info._has_parent_ = true;
					add_edge(sub_image, nullptr);
				} else if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kIsAlias) {
					// Is ImageAlias
					sub_image->GetPointedResource()->Visit([](auto *sub_image) -> void {
						if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kIsCombinedOrManagedImage)
							sub_image->m_internal_info._has_parent_ = true;
					});
					add_edge_and_visit_dep_pass(sub_image->GetPointedResource(), sub_image->GetProducerPass());
				} else
					assert(false);
			});
		} else {
			if constexpr (kClass == ResourceClass::kManagedImage) {
				m_compile_info._internal_image_set_.insert(resource);
				add_edge(resource, nullptr);
			} else if constexpr (kClass == ResourceClass::kManagedBuffer) {
				m_compile_info._internal_buffer_set_.insert(resource);
				add_edge(resource, nullptr);
			} else if constexpr (GetResourceState(kClass) == ResourceState::kAlias)
				add_edge_and_visit_dep_pass(resource->GetPointedResource(), resource->GetProducerPass());
		}
	});
}

void RenderGraphBase::_insert_write_after_read_edges() const {
	std::unordered_map<const ResourceBase *, const PassBase *> write_outputs;
	for (auto &pair : m_compile_info._pass_graph_) {
		auto &node = pair.second;
		for (const auto *p_edge : node.output_edges) {
			if (p_edge->usage != Usage::___USAGE_NUM && !UsageIsReadOnly(p_edge->usage)) {
				assert(write_outputs.find(p_edge->resource) ==
				       write_outputs.end()); // An output can only be written once
				write_outputs[p_edge->resource] = p_edge->to;
			}
		}
		for (const auto *p_edge : node.output_edges) {
			if (p_edge->usage != Usage::___USAGE_NUM && UsageIsReadOnly(p_edge->usage)) {
				auto it = write_outputs.find(p_edge->resource);
				if (it != write_outputs.end())
					_pass_graph_add_edge(p_edge->to, it->second, p_edge->resource, Usage::___USAGE_NUM);
			}
		}
		write_outputs.clear();
	}
}

void RenderGraphBase::_extract_passes() const {
	m_compile_info.passes.clear();

	std::queue<const PassBase *> candidate_queue;

	assert(m_compile_info._pass_graph_.find(nullptr) != m_compile_info._pass_graph_.end());
	assert(m_compile_info._pass_graph_[nullptr].in_degree == 0);
	for (auto *p_edge : m_compile_info._pass_graph_[nullptr].output_edges) {
		--m_compile_info._pass_graph_[p_edge->to].in_degree;
	}
	m_compile_info._pass_graph_.erase(nullptr);
	for (const auto &it : m_compile_info._pass_graph_) {
		if (it.second.in_degree == 0) {
			candidate_queue.push(it.first);
		}
	}

	PassInfo pass_info = {};
	while (!candidate_queue.empty()) {
		const PassBase *pass = candidate_queue.front();
		candidate_queue.pop();

		pass->m_internal_info.id = m_compile_info.passes.size();
		pass_info.pass = pass;
		m_compile_info.passes.push_back(pass_info);

		for (auto *p_edge : m_compile_info._pass_graph_[pass].output_edges) {
			uint32_t degree = --m_compile_info._pass_graph_[p_edge->to].in_degree;
			if (degree == 0)
				candidate_queue.push(p_edge->to);
		}
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
	{ // Generate Pass Graph
		m_compile_info._pass_graph_.clear();
		m_compile_info._pass_graph_edges_.clear();

		for (auto it = m_p_result_pool_data->pool.begin(); it != m_p_result_pool_data->pool.end(); ++it)
			_visit_resource_dep_passes(*m_p_result_pool_data->ValueGet<0, ResourceBase *>(it));
		_insert_write_after_read_edges();
		_extract_passes();

		m_compile_info._pass_graph_.clear();
		m_compile_info._pass_graph_edges_.clear();
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

	{ // Clean and Generate Internal Image & ImageView List
		m_compile_info.internal_images.clear();
		m_compile_info.internal_image_views.clear();
		// Initialize Internal Image View
		m_compile_info.internal_image_views.reserve(m_compile_info._internal_image_set_.size());
		// Also Add all top-level ManagedImages to the ManagedImage List
		for (auto *image : m_compile_info._internal_image_set_) {
			image->Visit([this](auto *image) -> void {
				if constexpr (ResourceVisitorTrait<decltype(image)>::kIsInternal) {
					image->m_internal_info.image_view_id = m_compile_info.internal_image_views.size();
					m_compile_info.internal_image_views.emplace_back();
					m_compile_info.internal_image_views.back().image = image;

					if (!image->m_internal_info._has_parent_) {
						image->m_internal_info.image_id = m_compile_info.internal_images.size();
						m_compile_info.internal_images.emplace_back();
						m_compile_info.internal_images.back().image = image;
					}
				}
			});
		}
		// Initialize Combined Image
		for (auto &image_info : m_compile_info.internal_images) {
			image_info.image->Visit([](auto *image) -> void {
				if constexpr (ResourceVisitorTrait<decltype(image)>::kClass == ResourceClass::kCombinedImage) {
					_initialize_combined_image(image);
				}
			});
		}
		m_compile_info._internal_image_set_.clear();
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
inline static void UpdateVkImageTypeFromVkImageViewType(VkImageType *p_image_type, VkImageViewType view_type) {
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
			// TODO: [Sus] Only Attachments need to be "alive" during the whole process ?
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
			else
				length = m_compile_info.passes[i].pass->m_p_attachment_data ? 1 : 0;

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
	// Visit Each Child Image, Update Size and Base Layer (Relative, need to be accumulated after)
	image->m_internal_info.size = {};
	image->ForEachExpandedImage([image](auto *sub_image) -> void {
		if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kIsCombinedOrManagedImage) {
			// Merge the Size of the Current Child Image
			if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kClass == ResourceClass::kManagedImage) {
				const auto &size = sub_image->GetSize();
				image->m_internal_info.size.Merge(size);
				sub_image->m_internal_info.base_layer =
				    image->m_internal_info.size.GetArrayLayers() - size.GetArrayLayers();
			} else if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kClass == ResourceClass::kCombinedImage) {
				_maintain_combined_image_size(sub_image); // Further Query SubImage Size
				image->m_internal_info.size.Merge(sub_image->m_internal_info.size);
				sub_image->m_internal_info.base_layer =
				    image->m_internal_info.size.GetArrayLayers() - sub_image->m_internal_info.size.GetArrayLayers();
			}
		}
	});
}

void RenderGraphBase::_accumulate_combined_image_base_layer(const CombinedImage *image) {
	uint32_t base_layer = image->m_internal_info.base_layer;
	image->ForEachExpandedImage([base_layer](auto *sub_image) -> void {
		if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kIsCombinedOrManagedImage) {
			sub_image->m_internal_info.base_layer += base_layer;
			if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kClass == ResourceClass::kCombinedImage)
				_accumulate_combined_image_base_layer(sub_image);
		}
	});
}

void RenderGraphBase::_create_vk_resource() const {
	// Query Resource Size
	for (auto &buffer_info : m_compile_info.internal_buffers) {
		VkBufferCreateInfo create_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
		create_info.usage = buffer_info.vk_buffer_usages;
		create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		create_info.size = buffer_info.buffer->GetSize();
		buffer_info.myvk_buffer = std::make_shared<RenderGraphBuffer>(this, create_info);
		vkGetBufferMemoryRequirements(GetDevicePtr()->GetHandle(), buffer_info.myvk_buffer->GetHandle(),
		                              &buffer_info.vk_memory_requirements);
	}
	for (auto &image_info : m_compile_info.internal_images) {
		const SubImageSize *p_image_size = image_info.image->Visit([](auto *image) -> const SubImageSize * {
			if constexpr (ResourceVisitorTrait<decltype(image)>::kIsInternal) {
				image->m_internal_info.parent = nullptr;
				image->m_internal_info.base_layer = 0;
				if constexpr (ResourceVisitorTrait<decltype(image)>::kClass == ResourceClass::kCombinedImage) {
					_maintain_combined_image_size(image);
					_accumulate_combined_image_base_layer(image);
					return &image->m_internal_info.size;
				} else
					return &image->GetSize();
			} else {
				assert(false);
				return nullptr;
			}
		});
		assert(p_image_size && p_image_size->GetBaseMipLevel() == 0);
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
				extent.width = p_image_size->GetExtent().width;
				create_info.mipLevels = p_image_size->GetMipLevels();
				create_info.arrayLayers = p_image_size->GetArrayLayers();
			} break;
			case VK_IMAGE_TYPE_2D: {
				extent.width = p_image_size->GetExtent().width;
				extent.height = p_image_size->GetExtent().height;
				create_info.mipLevels = p_image_size->GetMipLevels();
				create_info.arrayLayers = p_image_size->GetArrayLayers();
			} break;
			case VK_IMAGE_TYPE_3D: {
				assert(p_image_size->GetExtent().depth == 1 || p_image_size->GetArrayLayers() == 1);
				extent.width = p_image_size->GetExtent().width;
				extent.height = p_image_size->GetExtent().height;
				extent.depth = std::max(p_image_size->GetExtent().depth, p_image_size->GetArrayLayers());
				create_info.mipLevels = p_image_size->GetMipLevels();
				create_info.arrayLayers = 1;
			} break;
			default:
				assert(false);
			}
		}
		image_info.myvk_image = std::make_shared<RenderGraphImage>(this, create_info);
		vkGetImageMemoryRequirements(GetDevicePtr()->GetHandle(), image_info.myvk_image->GetHandle(),
		                             &image_info.vk_memory_requirements);
	}
}

inline static constexpr VkDeviceSize DivRoundUp(VkDeviceSize l, VkDeviceSize r) { return (l / r) + (l % r ? 1 : 0); }

void RenderGraphBase::_make_naive_allocation(MemoryInfo &&memory_info,
                                             const VmaAllocationCreateInfo &allocation_create_info) const {
	if (memory_info.empty())
		return;

	uint32_t allocation_id = m_compile_info.allocations.size();
	m_compile_info.allocations.emplace_back();
	auto &allocation_info = m_compile_info.allocations.back();

	VkMemoryRequirements memory_requirements = {};
	memory_requirements.alignment = memory_info.alignment;
	memory_requirements.memoryTypeBits = memory_info.memory_type_bits;

	VkDeviceSize allocation_blocks = 0;
	for (auto *p_resource_info : memory_info.resources) {
		p_resource_info->allocation_id = allocation_id;
		p_resource_info->memory_offset = allocation_blocks * memory_info.alignment;
		allocation_blocks += DivRoundUp(p_resource_info->vk_memory_requirements.size, memory_info.alignment);
	}
	memory_requirements.size = allocation_blocks * memory_info.alignment;

	allocation_info.myvk_allocation =
	    std::make_shared<RenderGraphAllocation>(this, memory_requirements, allocation_create_info);
}

// An AABB indicates a placed resource
struct MemAABB {
	struct Vec2 {
		VkDeviceSize mem;
		uint32_t pass;
	};
	Vec2 low, high;
	inline bool intersect(uint32_t first_pass, uint32_t last_pass) const {
		return high.pass >= first_pass && low.pass <= last_pass;
	}
};
struct MemEvent {
	VkDeviceSize mem;
	uint32_t cnt;
	inline bool operator<(const MemEvent &r) const { return mem < r.mem; }
};
void RenderGraphBase::_make_optimal_allocation(MemoryInfo &&memory_info,
                                               const VmaAllocationCreateInfo &allocation_create_info) const {
	if (memory_info.empty())
		return;

	uint32_t allocation_id = m_compile_info.allocations.size();
	m_compile_info.allocations.emplace_back();
	auto &allocation_info = m_compile_info.allocations.back();

	// Sort Resources by required sizes, place large resources first
	std::sort(memory_info.resources.begin(), memory_info.resources.end(),
	          [](const InternalResourceInfo *l, const InternalResourceInfo *r) -> bool {
		          return l->vk_memory_requirements.size > r->vk_memory_requirements.size ||
		                 (l->vk_memory_requirements.size == r->vk_memory_requirements.size &&
		                  l->first_pass < r->first_pass) ||
		                 (l->vk_memory_requirements.size == r->vk_memory_requirements.size &&
		                  l->first_pass == r->first_pass && l->last_pass > r->last_pass);
	          });

	VkDeviceSize allocation_blocks = 0;
	{
		std::vector<MemAABB> placed_blocks;
		std::vector<MemEvent> events;
		placed_blocks.reserve(memory_info.resources.size());
		events.reserve(memory_info.resources.size() << 1u);

		for (auto *p_resource_info : memory_info.resources) {
			// Find an empty position to place
			events.clear();
			for (const auto &placed : placed_blocks) {
				if (placed.intersect(p_resource_info->first_pass, p_resource_info->last_pass)) {
					events.push_back({placed.low.mem, 1});
					events.push_back({placed.high.mem, (uint32_t)-1});
				}
			}
			std::sort(events.begin(), events.end());

			VkDeviceSize required_mem_size =
			    DivRoundUp(p_resource_info->vk_memory_requirements.size, memory_info.alignment);

			VkDeviceSize optimal_mem_pos = 0, optimal_mem_size = std::numeric_limits<VkDeviceSize>::max();
			if (!events.empty()) {
				assert(events.front().cnt == 1 && events.back().cnt == -1);
				if (events.front().mem >= required_mem_size)
					optimal_mem_size = events.front().mem;
				else
					optimal_mem_pos = events.back().mem;

				for (uint32_t i = 1; i < events.size(); ++i) {
					events[i].cnt += events[i - 1].cnt;
					if (events[i - 1].cnt == 0 && events[i].cnt == 1) {
						VkDeviceSize cur_mem_pos = events[i - 1].mem, cur_mem_size = events[i].mem - events[i - 1].mem;
						if (required_mem_size <= cur_mem_size && cur_mem_size < optimal_mem_size) {
							optimal_mem_size = cur_mem_size;
							optimal_mem_pos = cur_mem_pos;
						}
					}
				}
			}

			p_resource_info->allocation_id = allocation_id;
			p_resource_info->memory_offset = optimal_mem_pos * memory_info.alignment;
			allocation_blocks = std::max(allocation_blocks, optimal_mem_pos + required_mem_size);

			placed_blocks.push_back({{optimal_mem_pos, p_resource_info->first_pass},
			                         {optimal_mem_pos + required_mem_size, p_resource_info->last_pass}});
		}
	}

	VkMemoryRequirements memory_requirements = {};
	memory_requirements.alignment = memory_info.alignment;
	memory_requirements.memoryTypeBits = memory_info.memory_type_bits;
	memory_requirements.size = allocation_blocks * memory_info.alignment;

	allocation_info.myvk_allocation =
	    std::make_shared<RenderGraphAllocation>(this, memory_requirements, allocation_create_info);
}

void RenderGraphBase::_create_and_bind_memory_allocation() const {
	m_compile_info.allocations.clear();
	{ // Create Allocations
		MemoryInfo device_memory{}, lazy_memory{}, mapped_memory{};

		device_memory.resources.reserve(m_compile_info.internal_images.size() + m_compile_info.internal_buffers.size());
		device_memory.alignment =
		    GetDevicePtr()->GetPhysicalDevicePtr()->GetProperties().vk10.limits.bufferImageGranularity;

		bool lazy_allocation_supported = false;
		for (uint32_t i = 0; i < GetDevicePtr()->GetPhysicalDevicePtr()->GetMemoryProperties().memoryTypeCount; i++) {
			if (GetDevicePtr()->GetPhysicalDevicePtr()->GetMemoryProperties().memoryTypes[i].propertyFlags &
			    VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) {
				lazy_allocation_supported = true;
				break;
			}
		}
		printf("lazy_allocation_supported = %d\n", lazy_allocation_supported);

		for (auto &image_info : m_compile_info.internal_images) {
			if (lazy_allocation_supported && image_info.is_transient)
				lazy_memory.push(&image_info); // If the image is Transient and LAZY_ALLOCATION is supported
			else
				device_memory.push(&image_info);
		}
		for (auto &buffer_info : m_compile_info.internal_buffers) {
			if (false) // TODO: Mapped Buffer Condition
				mapped_memory.push(&buffer_info);
			else
				device_memory.push(&buffer_info);
		}
		{
			VmaAllocationCreateInfo create_info = {};
			create_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT | VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT;
			create_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			_make_optimal_allocation(std::move(device_memory), create_info);
		}
		{
			VmaAllocationCreateInfo create_info = {};
			create_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
			create_info.usage = VMA_MEMORY_USAGE_GPU_LAZILY_ALLOCATED;
			_make_naive_allocation(std::move(lazy_memory), create_info);
		}
	}
	// Bind Memory
	for (auto &image_info : m_compile_info.internal_images) {
		vmaBindImageMemory2(GetDevicePtr()->GetAllocatorHandle(),
		                    m_compile_info.allocations[image_info.allocation_id].myvk_allocation->GetHandle(),
		                    image_info.memory_offset, image_info.myvk_image->GetHandle(), nullptr);
	}
	for (auto &buffer_info : m_compile_info.internal_buffers) {
		vmaBindBufferMemory2(GetDevicePtr()->GetAllocatorHandle(),
		                     m_compile_info.allocations[buffer_info.allocation_id].myvk_allocation->GetHandle(),
		                     buffer_info.memory_offset, buffer_info.myvk_buffer->GetHandle(), nullptr);
	}
	for (const auto &allocation_info : m_compile_info.allocations) {
		VmaAllocationInfo info;
		vmaGetAllocationInfo(GetDevicePtr()->GetAllocatorHandle(), allocation_info.myvk_allocation->GetHandle(), &info);
		VkMemoryPropertyFlags flags;
		vmaGetAllocationMemoryProperties(GetDevicePtr()->GetAllocatorHandle(),
		                                 allocation_info.myvk_allocation->GetHandle(), &flags);
		printf("allocation: size = %lu MB, memory_type = %u\n", info.size >> 20u, flags);
	}
}

void RenderGraphBase::generate_vk_resource() const {
	_create_vk_resource();
	_create_and_bind_memory_allocation();
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
		          << ", " << image_info.vk_memory_requirements.memoryTypeBits << "}"
		          << " transient = " << image_info.is_transient << " offset = " << image_info.memory_offset
		          << std::endl;
	}

	for (const auto &buffer_info : m_compile_info.internal_buffers) {
		auto buffer = buffer_info.buffer;
		std::cout << buffer->GetKey().GetName() << ":" << buffer->GetKey().GetID() << " [" << buffer_info.first_pass
		          << ", " << buffer_info.last_pass << "]"
		          << " {size, alignment, flag} = {" << buffer_info.vk_memory_requirements.size << ", "
		          << buffer_info.vk_memory_requirements.alignment << ", "
		          << buffer_info.vk_memory_requirements.memoryTypeBits << "}"
		          << " offset = " << buffer_info.memory_offset << std::endl;
	}
}

inline static constexpr VkImageAspectFlags VkImageAspectFlagsFromVkFormat(VkFormat format) {
	switch (format) {
	case VK_FORMAT_D32_SFLOAT:
	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_X8_D24_UNORM_PACK32:
		return VK_IMAGE_ASPECT_DEPTH_BIT;
	case VK_FORMAT_D16_UNORM_S8_UINT:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	case VK_FORMAT_S8_UINT:
		return VK_IMAGE_ASPECT_STENCIL_BIT;
	default:
		return VK_IMAGE_ASPECT_COLOR_BIT;
	}
}

void RenderGraphBase::generate_vk_image_view() const {
	for (auto &image_view_info : m_compile_info.internal_image_views) {
		auto [image_id, image_view_create_info] =
		    image_view_info.image->Visit([](const auto *image) -> std::tuple<uint32_t, VkImageViewCreateInfo> {
			    if constexpr (ResourceVisitorTrait<decltype(image)>::kIsInternal) {
				    const SubImageSize *p_image_size = {};
				    if constexpr (ResourceVisitorTrait<decltype(image)>::kClass == ResourceClass::kCombinedImage)
					    p_image_size = &image->m_internal_info.size;
				    else
					    p_image_size = &image->GetSize();

				    VkImageViewCreateInfo create_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
				    create_info.format = image->GetFormat();
				    create_info.viewType = image->GetViewType();
				    create_info.subresourceRange.baseArrayLayer = image->m_internal_info.base_layer;
				    create_info.subresourceRange.layerCount = p_image_size->GetArrayLayers();
				    create_info.subresourceRange.baseMipLevel = p_image_size->GetBaseMipLevel();
				    create_info.subresourceRange.levelCount = p_image_size->GetMipLevels();
				    create_info.subresourceRange.aspectMask = VkImageAspectFlagsFromVkFormat(image->GetFormat());
				    printf("ImageView: {image_id = %u, base_layer = %u, layers = %u, base_level = %u, levels = %u}\n",
				           image->m_internal_info.image_id, create_info.subresourceRange.baseArrayLayer,
				           create_info.subresourceRange.layerCount, create_info.subresourceRange.baseMipLevel,
				           create_info.subresourceRange.levelCount);
				    return {image->m_internal_info.image_id, create_info};
			    } else {
				    assert(false);
				    return {};
			    }
		    });
		image_view_info.myvk_image_view =
		    myvk::ImageView::Create(m_compile_info.internal_images[image_id].myvk_image, image_view_create_info);
	}
}

inline static constexpr VkShaderStageFlags VkShaderStagesFromVkPipelineStages(VkPipelineStageFlags2 pipeline_stages) {
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

} // namespace myvk_rg::_details_
