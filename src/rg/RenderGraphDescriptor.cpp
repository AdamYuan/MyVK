#include "RenderGraphDescriptor.hpp"

#include "VkHelper.hpp"

namespace myvk_rg::_details_ {

void RenderGraphDescriptor::Create(const myvk::Ptr<myvk::Device> &device, const RenderGraphResolver &resolved) {
	m_p_resolved = &resolved;

	m_pass_descriptors.clear();
	m_pass_descriptors.resize(resolved.GetPassNodeCount());

	std::unordered_map<VkDescriptorType, uint32_t> descriptor_type_counts;

	std::vector<myvk::Ptr<myvk::DescriptorSetLayout>> descriptor_set_layouts;

	for (uint32_t i = 0; i < resolved.GetPassNodeCount(); ++i) {
		const PassBase *pass = resolved.GetPassNode(i).pass;

		if (pass->m_p_descriptor_set_data == nullptr || pass->m_p_descriptor_set_data->m_bindings.empty())
			continue;

		const auto &binding_map = pass->m_p_descriptor_set_data->m_bindings;
		std::vector<VkDescriptorSetLayoutBinding> bindings;
		std::vector<VkSampler> immutable_samplers;
		bindings.reserve(binding_map.size());
		immutable_samplers.reserve(binding_map.size());

		for (const auto &binding_data : binding_map) {
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

			++descriptor_type_counts[info.descriptorType];
		}
		descriptor_set_layouts.emplace_back(myvk::DescriptorSetLayout::Create(device, bindings));
		// TODO: DescriptorSetLayout Create Callbacks
	}

	myvk::Ptr<myvk::DescriptorPool> descriptor_pool;
	{
		std::vector<VkDescriptorPoolSize> pool_sizes;
		pool_sizes.reserve(descriptor_type_counts.size());
		for (const auto &it : descriptor_type_counts) {
			pool_sizes.emplace_back();
			VkDescriptorPoolSize &size = pool_sizes.back();
			size.type = it.first;
			size.descriptorCount = it.second;
		}
		descriptor_pool = myvk::DescriptorPool::Create(device, descriptor_set_layouts.size(), pool_sizes);
	}

	std::vector<myvk::Ptr<myvk::DescriptorSet>> descriptor_sets =
	    myvk::DescriptorSet::CreateMultiple(descriptor_pool, descriptor_set_layouts);

	for (uint32_t i = 0, s = 0; i < resolved.GetPassNodeCount(); ++i) {
		const PassBase *pass = resolved.GetPassNode(i).pass;

		if (pass->m_p_descriptor_set_data == nullptr || pass->m_p_descriptor_set_data->m_bindings.empty())
			continue;

		m_pass_descriptors[i].set = descriptor_sets[s++];
	}

	printf("Descriptor Created\n");
}

} // namespace myvk_rg::_details_