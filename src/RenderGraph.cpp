#include "myvk/RenderGraph.hpp"

namespace myvk {

const Ptr<Device> &RenderGraphPassBase::GetDevicePtr() const { return LockRenderGraph()->GetDevicePtr(); }

void RenderGraphDescriptorPassBase::create_descriptors_layout() {
	std::vector<VkDescriptorSetLayoutBinding> bindings(m_descriptor_group.GetSize());
	std::vector<VkSampler> immutable_samplers;
	for (uint32_t i = 0; i < m_descriptor_group.GetSize(); ++i) {
		auto &binding = bindings[i];
		auto &descriptor_info = m_descriptor_group[i].m_descriptor_info;
		binding.binding = i;
		binding.descriptorType = RenderGraphInputUsageGetDescriptorType(m_descriptor_group[i].GetUsage());
		binding.descriptorCount = 1;
		binding.stageFlags = descriptor_info.stage_flags;
		if (m_descriptor_group[i].GetUsage() == RenderGraphInputUsage::kSampledImage) {
			immutable_samplers.push_back(
			    descriptor_info.sampler
			        ? (descriptor_info.sampler->GetHandle())
			        : (descriptor_info.sampler = LockRenderGraph()->get_sampler(descriptor_info.sampler_info))
			              ->GetHandle());
			binding.pImmutableSamplers = &immutable_samplers.back();
		}
	}
	m_descriptor_layout = myvk::DescriptorSetLayout::Create(GetDevicePtr(), bindings);
}

Ptr<Sampler> RenderGraphBase::get_sampler(const RenderGraphSamplerInfo &sampler_info) {
	auto cache_it = m_sampler_cache.find(sampler_info);
	if (cache_it != m_sampler_cache.end()) {
		auto cache = cache_it->second.lock();
		if (cache)
			return cache;
	}
	auto ret = Sampler::Create(m_device_ptr, sampler_info.filter, sampler_info.address_mode, sampler_info.mipmap_mode,
	                           VK_LOD_CLAMP_NONE);
	m_sampler_cache[sampler_info] = ret;
	return ret;
}
/* void RenderGraph::compile() {
	if (!m_recompile_flag)
		return;
	m_recompile_flag = false;
	// Then ?
}
void RenderGraph::update(const std::function<void(RenderGraphInfo &)> &builder) {
	RenderGraphInfo info{GetSelfPtr<RenderGraph>()};
	builder(info);
	m_passes = std::move(info.m_passes);
	m_outputs = std::move(info.m_outputs);
	SetRecompile();
} */
} // namespace myvk
