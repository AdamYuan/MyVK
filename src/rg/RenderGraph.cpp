#include "myvk_rg/RenderGraph.hpp"
#include "myvk_rg/_details_/RenderGraphBase.hpp"

#include <iostream>

namespace myvk_rg::_details_ {

void RenderGraphBase::_visit_resource_dep_pass(const ResourceBase *resource) const {
	// Mark Visited Pass, Generate Temporal Managed Resource Sets
	const auto visit_dep_pass = [this](const PassBase *dep_pass) -> void {
		if (dep_pass && !dep_pass->m_internal_info.visited) {
			dep_pass->m_internal_info.visited = true;
			// Further Traverse dep_pass's dependent Resources
			dep_pass->for_each_input(
			    [this](const Input *p_input) { _visit_resource_dep_pass(p_input->GetResource()); });
		}
	};
	resource->Visit([this, &visit_dep_pass](auto *resource) -> void {
		constexpr auto kClass = ResourceVisitorTrait<decltype(resource)>::kClass;
		// For CombinedImage, further For Each its Child Images
		if constexpr (kClass == ResourceClass::kCombinedImage) {
			if (resource->m_internal_info._has_parent_ == false)
				m_compile_info._managed_image_set_.insert(resource);
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
					m_compile_info._managed_image_set_.insert(resource);
			} else if constexpr (kClass == ResourceClass::kManagedBuffer) {
				m_compile_info._managed_buffer_set_.insert(resource);
			} else if constexpr (GetResourceState(kClass) == ResourceState::kAlias)
				visit_dep_pass(resource->GetProducerPass());
		}
	});
}
void RenderGraphBase::_traverse_combined_image(const CombinedImage *image) {
	// Visit Each Child Image, Update Size and Base Layer
	image->m_internal_info.size = {};
	auto *parent = image->m_internal_info.parent ? image->m_internal_info.parent : image;
	image->ForEachExpandedImage([image, parent](auto *sub_image) -> void {
		if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kIsCombinedOrManagedImage) {
			sub_image->m_internal_info.image_id = image->m_internal_info.image_id;
			sub_image->m_internal_info.parent = parent;
			sub_image->m_internal_info.base_layer =
			    image->m_internal_info.base_layer + image->m_internal_info.size.GetArrayLayers();
			sub_image->m_internal_info._has_parent_ = false; // Restore _has_parent_

			// Merge the Size of the Current Child Image
			if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kClass == ResourceClass::kManagedImage) {
				image->m_internal_info.size.Merge(sub_image->GetSize());
			} else if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kClass == ResourceClass::kCombinedImage) {
				_traverse_combined_image(sub_image); // Further Query SubImage Size
				image->m_internal_info.size.Merge(sub_image->m_internal_info.size);
			}
		}
	});
}

void RenderGraphBase::_extract_visited_pass(const std::vector<PassBase *> *p_cur_seq) const {
	PassInfo pass_info = {};
	for (const auto pass : *p_cur_seq) {
		if (pass->m_internal_info.visited) {
			pass->m_internal_info.visited = false; // Restore
			pass->m_internal_info.id = m_compile_info.pass_sequence.size();
			pass_info.pass = pass;
			m_compile_info.pass_sequence.push_back(pass_info);
		} else if (pass->m_p_pass_pool_sequence)
			_extract_visited_pass(pass->m_p_pass_pool_sequence);
	}
}

void RenderGraphBase::assign_pass_resource_indices() const {
	{ // Generate Pass Sequence
		m_compile_info.pass_sequence.clear();
		for (auto it = m_p_result_pool_data->pool.begin(); it != m_p_result_pool_data->pool.end(); ++it)
			_visit_resource_dep_pass(*m_p_result_pool_data->ValueGet<0, ResourceBase *>(it));
		_extract_visited_pass(m_p_pass_pool_sequence);
	}

	{ // Generate ManagedBuffer List
		m_compile_info.managed_buffers.clear();
		m_compile_info.managed_buffers.reserve(m_compile_info._managed_buffer_set_.size());
		for (auto *buffer : m_compile_info._managed_buffer_set_) {
			buffer->m_internal_info.buffer_id = m_compile_info.managed_buffers.size();
			m_compile_info.managed_buffers.push_back(buffer);
		}
		m_compile_info._managed_buffer_set_.clear();
	}

	{ // Generate ManagedImage List
		m_compile_info.managed_images.clear();
		// Add all top-level ManagedImages to the ManagedImage List
		for (auto *image : m_compile_info._managed_image_set_) {
			image->Visit([this](auto *image) -> void {
				if constexpr (ResourceVisitorTrait<decltype(image)>::kIsCombinedOrManagedImage) {
					if (!image->m_internal_info._has_parent_) {
						image->m_internal_info.image_id = m_compile_info.managed_images.size();
						m_compile_info.managed_images.push_back(image);
					}
				}
			});
		}
		m_compile_info._managed_image_set_.clear();
	}

	printf("managed image count: %ld\n", m_compile_info.managed_images.size());
	printf("managed buffer count: %ld\n", m_compile_info.managed_buffers.size());
}
void RenderGraphBase::merge_subpass() const {
	// Calculate _merge_length_, Complexity: O(N + M)
	// _merge_length_ == 0: The pass is not a graphics pass
	// _merge_length_ == 1: The pass is a graphics pass, but can't be merged
	// _merge_length_ >  1: The pass is a graphics pass, and it can be merged to a group of _merge_length_ with the
	// passes before
	m_compile_info.pass_sequence[0]._merge_length_ =
	    m_compile_info.pass_sequence[0].pass->m_p_attachment_data ? 1u : 0u;
	for (uint32_t i = 1; i < m_compile_info.pass_sequence.size(); ++i) {
		auto &cur_pass_info = m_compile_info.pass_sequence[i];
		const auto &prev_pass_info = m_compile_info.pass_sequence[i - 1];
		cur_pass_info._merge_length_ = cur_pass_info.pass->m_p_attachment_data ? prev_pass_info._merge_length_ + 1 : 0;
	}
	for (auto &pass_info : m_compile_info.pass_sequence) {
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
	for (const auto &pass_info : m_compile_info.pass_sequence) {
		auto pass = pass_info.pass;
		std::cout << pass->GetKey().GetName() << ":" << pass->GetKey().GetID() << ".id = " << pass->m_internal_info.id
		          << " .merge_len = " << pass_info._merge_length_ << std::endl;
	}
}
void RenderGraphBase::generate_vk_resource() const {
	// Generate CombinedImage Data
	for (auto *image : m_compile_info.managed_images) {
		image->Visit([](auto *image) -> void {
			if constexpr (ResourceVisitorTrait<decltype(image)>::kIsCombinedOrManagedImage) {
				image->m_internal_info.parent = nullptr;
				image->m_internal_info.base_layer = 0;
				if constexpr (ResourceVisitorTrait<decltype(image)>::kClass == ResourceClass::kCombinedImage) {
					_traverse_combined_image(image);
				}
			}
		});
	}
	/* for (auto image : m_compile_info.managed_images) {
	    std::cout << image->GetKey().GetName() << ":" << image->GetKey().GetID()
	              << " mip_levels = " << image->Visit([](auto *image) -> uint32_t {
	                     if constexpr (ResourceVisitorTrait<decltype(image)>::kClass == ResourceClass::kCombinedImage)
	                         return image->m_internal_info.size.GetMipLevels();
	                     else if constexpr (ResourceVisitorTrait<decltype(image)>::kClass ==
	                                        ResourceClass::kManagedImage)
	                         return image->GetSize().GetMipLevels();
	                     return 0;
	                 })
	              << std::endl;
	} */
}

inline constexpr VkShaderStageFlags ShaderStagesFromPipelineStages(VkPipelineStageFlags2 pipeline_stages) {
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
				    ShaderStagesFromPipelineStages(binding_data.second.GetInputPtr()->GetUsagePipelineStages());
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
