#include "myvk/RenderGraph2.hpp"
#include <iostream>

namespace myvk::render_graph {

void RenderGraphBase::pass_graph_traverse(RGPassBase *pass) const {
	assert(!pass->m_traversal_data.in_stack); // Detect cycle
	pass->m_traversal_data.in_stack = true;
	for (auto it = pass->m_p_input_pool_data->pool.begin(); it != pass->m_p_input_pool_data->pool.end(); ++it) {
		auto dep_input = pass->m_p_input_pool_data->ValueGet<0, RGInput>(it);
		auto dep_resource = dep_input->GetResource();
		auto dep_pass = dep_resource->GetProducerPassPtr();
		// Skip internal resource inputs
		if (dep_pass != pass && dep_pass->m_traversal_data.in_degree++ == 0)
			pass_graph_traverse(dep_pass);
	}
	m_pass_graph_nodes.push_back(pass);
	pass->m_traversal_data.in_stack = false;
}
void RenderGraphBase::DebugTraverse() const {
	for (auto &node : m_pass_graph_nodes)
		node->m_traversal_data = {};
	m_pass_graph_nodes.clear();

	for (auto it = m_p_result_pool_data->pool.begin(); it != m_p_result_pool_data->pool.end(); ++it) {
		RGResourceBase *resource = *m_p_result_pool_data->ValueGet<0, RGResourceBase *>(it);
		pass_graph_traverse(resource->GetProducerPassPtr());
	}

	for (auto &node : m_pass_graph_nodes) {
		std::cout << node->GetKey().GetName() << ":" << node->GetKey().GetID()
		          << ".degree = " << node->m_traversal_data.in_degree << std::endl;
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
const Ptr<DescriptorSetLayout> &RGDescriptorSetData::GetVkDescriptorSetLayout() const {
	if (m_updated) {
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
				info.descriptorType = RGUsageGetDescriptorType(binding_data.second.GetInputPtr()->GetUsage());
				info.descriptorCount = 1;
				info.stageFlags =
				    ShaderStagesFromPipelineStages(binding_data.second.GetInputPtr()->GetUsagePipelineStages());
				if (info.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
				    binding_data.second.GetVkSampler()) {
					immutable_samplers.push_back(binding_data.second.GetVkSampler()->GetHandle());
					info.pImmutableSamplers = &immutable_samplers.back();
				}
			}
			m_descriptor_set_layout = myvk::DescriptorSetLayout::Create(GetRenderGraphPtr()->GetDevicePtr(), bindings);
		}
		m_updated = false;
	}
	return m_descriptor_set_layout;
}
} // namespace myvk::render_graph
