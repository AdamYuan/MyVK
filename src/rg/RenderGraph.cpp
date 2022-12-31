#include "myvk_rg/RenderGraph.hpp"
#include <iostream>

namespace myvk_rg::_details_ {

void RenderGraphBase::_visit_pass_graph(PassBase *pass) const {
	const auto visit_dep = [this, pass](PassBase *dep_pass) -> void {
		if (dep_pass && dep_pass != pass && !dep_pass->m_traversal_data.visited) {
			dep_pass->m_traversal_data.visited = true;
			_visit_pass_graph(dep_pass);
		}
	};
	for (auto it = pass->m_p_input_pool_data->pool.begin(); it != pass->m_p_input_pool_data->pool.end(); ++it) {
		auto dep_input = pass->m_p_input_pool_data->ValueGet<0, Input>(it);
		auto dep_resource = dep_input->GetResource();
		dep_resource->Visit([visit_dep](auto *resource) -> void {
			if constexpr (ResourceVisitorTrait<decltype(resource)>::kClass == ResourceClass::kCombinedImage) {
				for (auto *sub_image : resource->GetImages())
					visit_dep(sub_image->GetProducerPassPtr());
			} else
				visit_dep(resource->GetProducerPassPtr());
		});
	}
}
void RenderGraphBase::_extract_visited_pass(const std::vector<PassBase *> *p_cur_seq) const {
	for (auto pass : *p_cur_seq) {
		if (pass->m_traversal_data.visited) {
			pass->m_traversal_data.index = m_pass_sequence.size();
			m_pass_sequence.push_back(pass);
		} else if (pass->m_p_pass_pool_sequence)
			_extract_visited_pass(pass->m_p_pass_pool_sequence);
	}
}
void RenderGraphBase::generate_pass_sequence() const {
	for (auto pass : m_pass_sequence)
		pass->m_traversal_data.visited = false;
	m_pass_sequence.clear();

	for (auto it = m_p_result_pool_data->pool.begin(); it != m_p_result_pool_data->pool.end(); ++it) {
		ResourceBase *resource = *m_p_result_pool_data->ValueGet<0, ResourceBase *>(it);
		if (resource->GetProducerPassPtr()) {
			resource->GetProducerPassPtr()->m_traversal_data.visited = true;
			_visit_pass_graph(resource->GetProducerPassPtr());
		}
	}
	_extract_visited_pass(m_p_pass_pool_sequence);
	for (auto pass : m_pass_sequence) {
		std::cout << pass->GetKey().GetName() << ":" << pass->GetKey().GetID()
		          << ".degree = " << pass->m_traversal_data.index << std::endl;
	}
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
