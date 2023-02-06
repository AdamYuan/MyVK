#include <myvk_rg/RenderGraph.hpp>

#include "RenderGraphAllocator.hpp"
#include "RenderGraphExecutor.hpp"
#include "RenderGraphResolver.hpp"
#include "RenderGraphScheduler.hpp"
#include "myvk_rg/_details_/RenderGraphBase.hpp"
#include "myvk_rg/_details_/Resource.hpp"

namespace myvk_rg::_details_ {

struct RenderGraphBase::Compiler {
	RenderGraphResolver resolver;
	RenderGraphScheduler scheduler;
	RenderGraphAllocator allocator;
	RenderGraphExecutor executor;
};

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

void RenderGraphBase::MYVK_RG_INITIALIZER_FUNC(const myvk::Ptr<myvk::Device> &device) {
	m_device_ptr = device;
	// Check Lazy Allocation Support
	for (uint32_t i = 0; i < GetDevicePtr()->GetPhysicalDevicePtr()->GetMemoryProperties().memoryTypeCount; i++) {
		if (GetDevicePtr()->GetPhysicalDevicePtr()->GetMemoryProperties().memoryTypes[i].propertyFlags &
		    VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) {
			m_lazy_allocation_supported = true;
			break;
		}
	}
	// Create Compiler
	m_compiler = std::make_unique<Compiler>();
}

RenderGraphBase::RenderGraphBase() = default;
RenderGraphBase::~RenderGraphBase() = default;

void RenderGraphBase::Compile() const {
	if (m_compile_phrase == 0u)
		return;

	/*
	 * The RenderGraph Compile Phrases
	 *
	 *            /--> Schedule --\
	 * Resolve --|                 |--> PrepareExecutor
	 *            \--> Allocate --/
	 */

	uint8_t exe_compile_phrase = m_compile_phrase;
	if (m_compile_phrase & CompilePhrase::kResolve)
		exe_compile_phrase |= CompilePhrase::kSchedule | CompilePhrase::kAllocate | CompilePhrase::kPrepareExecutor;
	if (m_compile_phrase & CompilePhrase::kAllocate)
		exe_compile_phrase |= CompilePhrase::kPrepareExecutor;
	if (m_compile_phrase & CompilePhrase::kSchedule)
		exe_compile_phrase |= CompilePhrase::kPrepareExecutor;
	m_compile_phrase = 0u;

	if (exe_compile_phrase & CompilePhrase::kResolve)
		m_compiler->resolver.Resolve(this);
	if (exe_compile_phrase & CompilePhrase::kSchedule)
		m_compiler->scheduler.Schedule(m_compiler->resolver);
	if (exe_compile_phrase & CompilePhrase::kAllocate)
		m_compiler->allocator.Allocate(GetDevicePtr(), m_compiler->resolver);
	if (exe_compile_phrase & CompilePhrase::kPrepareExecutor)
		m_compiler->executor.Prepare(GetDevicePtr(), m_compiler->resolver, m_compiler->scheduler,
		                             m_compiler->allocator);
}

void RenderGraphBase::CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const {
	Compile();
	m_compiler->executor.CmdExecute(command_buffer);
}

// Resource GetVk functions
const myvk::Ptr<myvk::BufferBase> &ManagedBuffer::GetVkBuffer() const {
	return GetRenderGraphPtr()->m_compiler->allocator.GetVkBuffer(this);
}
const myvk::Ptr<myvk::ImageView> &ManagedImage::GetVkImageView() const {
	return GetRenderGraphPtr()->m_compiler->allocator.GetVkImageView(this);
}
const myvk::Ptr<myvk::ImageView> &CombinedImage::GetVkImageView() const {
	return GetRenderGraphPtr()->m_compiler->allocator.GetVkImageView(this);
}

} // namespace myvk_rg::_details_
