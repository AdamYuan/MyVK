#include "myvk/RenderGraph2.hpp"
#include <cassert>

namespace myvk::render_graph {

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
const Ptr<DescriptorSetLayout> &RGDescriptorSet::GetDescriptorSetLayout() const {
	if (m_updated || !m_descriptor_set_layout) {
		std::vector<VkDescriptorSetLayoutBinding> bindings;
		std::vector<VkSampler> immutable_samplers;
		bindings.reserve(m_bindings.size());
		immutable_samplers.reserve(m_bindings.size());

		for (const auto &binding_data : m_bindings) {
			VkDescriptorSetLayoutBinding info = {};
			info.binding = binding_data.first;
			info.descriptorType = RGUsageGetDescriptorType(binding_data.second.GetInputPtr()->GetUsage());
			info.descriptorCount = 1;
			info.stageFlags =
			    ShaderStagesFromPipelineStages(binding_data.second.GetInputPtr()->GetUsagePipelineStages());
			if (binding_data.second.GetSampler()) {
				immutable_samplers.push_back(binding_data.second.GetSampler()->GetHandle());
				info.pImmutableSamplers = &immutable_samplers.back();
			}
		}
		m_descriptor_set_layout = myvk::DescriptorSetLayout::Create(GetRenderGraphPtr()->GetDevicePtr(), bindings);

		m_updated = false;
	}
	return m_descriptor_set_layout;
}
} // namespace myvk::render_graph
