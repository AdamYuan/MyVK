// #include "myvk/RenderGraph.hpp"
#include "myvk/RenderGraph2.hpp"

namespace myvk {

/* void RenderGraphDescriptorPassBase::create_descriptors_layout() {
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
} */
} // namespace myvk
