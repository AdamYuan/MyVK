#include "myvk_rg/RenderGraph.hpp"
#include "myvk_rg/_details_/RenderGraphBase.hpp"

#include <iostream>

namespace myvk_rg::_details_ {

void RenderGraphBase::_visit_resource_dep_pass(const PassBase *pass, const ResourceBase *resource) const {
	// Mark Visited Pass, Generate Temporal Managed Resource Sets
	const auto visit_dep_pass = [this, pass](const PassBase *dep_pass) -> void {
		if (dep_pass && dep_pass != pass && !dep_pass->m_internal_info.visited) {
			dep_pass->m_internal_info.visited = true;
			_traverse_pass_graph(dep_pass);
		}
	};
	resource->Visit([this, visit_dep_pass](auto *resource) -> void {
		constexpr auto kClass = ResourceVisitorTrait<decltype(resource)>::kClass;

		// For CombinedImage, further For Each its Child Images
		if constexpr (kClass == ResourceClass::kCombinedImage) {
			if (resource->m_internal_info._has_parent_ == false)
				m_compile_info._managed_image_set_.insert(resource);
			// Visit Each SubImage
			resource->ForEachImage([visit_dep_pass](auto *sub_image) -> void {
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
void RenderGraphBase::_traverse_pass_graph(const PassBase *pass) const {
	// For Each Input
	for (auto it = pass->m_p_input_pool_data->pool.begin(); it != pass->m_p_input_pool_data->pool.end(); ++it) {
		_visit_resource_dep_pass(pass, pass->m_p_input_pool_data->ValueGet<0, Input>(it)->GetResource());
	}
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
	for (auto pass : *p_cur_seq) {
		if (pass->m_internal_info.visited) {
			pass->m_internal_info.visited = false; // Restore
			pass->m_internal_info.id = m_compile_info.pass_sequence.size();
			m_compile_info.pass_sequence.push_back(pass);
		} else if (pass->m_p_pass_pool_sequence)
			_extract_visited_pass(pass->m_p_pass_pool_sequence);
	}
}
void RenderGraphBase::assign_pass_resource_indices() const {
	{ // Generate Pass Sequence
		m_compile_info.pass_sequence.clear();
		for (auto it = m_p_result_pool_data->pool.begin(); it != m_p_result_pool_data->pool.end(); ++it) {
			ResourceBase *resource = *m_p_result_pool_data->ValueGet<0, ResourceBase *>(it);
			_visit_resource_dep_pass(nullptr, resource);
		}
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
	}

	for (auto pass : m_compile_info.pass_sequence) {
		std::cout << pass->GetKey().GetName() << ":" << pass->GetKey().GetID() << ".id = " << pass->m_internal_info.id
		          << std::endl;
	}
	printf("managed image count: %ld\n", m_compile_info.managed_images.size());
	for (auto image : m_compile_info.managed_images) {
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
	}
	printf("managed buffer count: %ld\n", m_compile_info.managed_buffers.size());
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
