#include <myvk_rg/RenderGraph.hpp>

#include "RenderGraphAllocator.hpp"
#include "RenderGraphDescriptor.hpp"
#include "RenderGraphExecutor.hpp"
#include "RenderGraphResolver.hpp"
#include "RenderGraphScheduler.hpp"

#include "myvk_rg/_details_/RenderGraphBase.hpp"
#include "myvk_rg/_details_/Resource.hpp"

namespace myvk_rg::_details_ {

struct RenderGraphBase::Compiler {
	RenderGraphResolver resolver;
	RenderGraphDescriptor descriptor;
	RenderGraphScheduler scheduler;
	RenderGraphAllocator allocator;
	RenderGraphExecutor executor;
};

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

void RenderGraphBase::compile() const {
#define CAST8(x) static_cast<uint8_t>(x)
	if (m_compile_phrase == 0u)
		return;

	/*
	 * The RenderGraph Compile Phrases
	 *
	 *            /-----> Schedule ------\
	 *           |                       |--> Prepare Executor
	 * Resolve --|------> Allocate -----<
	 *           |                       |--> Pre-bind Descriptor
	 *            \-> Create Descriptor -/
	 */

	uint8_t exe_compile_phrase = m_compile_phrase;
	if (m_compile_phrase & CAST8(CompilePhrase::kResolve))
		exe_compile_phrase |=
		    CAST8(CompilePhrase::kSchedule | CompilePhrase::kCreateDescriptor | CompilePhrase::kAllocate |
		          CompilePhrase::kPrepareExecutor | CompilePhrase::kPreBindDescriptor);
	if (m_compile_phrase & CAST8(CompilePhrase::kAllocate))
		exe_compile_phrase |= CAST8(CompilePhrase::kPrepareExecutor | CompilePhrase::kPreBindDescriptor);
	if (m_compile_phrase & CAST8(CompilePhrase::kSchedule))
		exe_compile_phrase |= CAST8(CompilePhrase::kPrepareExecutor);
	if (m_compile_phrase & CAST8(CompilePhrase::kCreateDescriptor))
		exe_compile_phrase |= CAST8(CompilePhrase::kPreBindDescriptor);
	m_compile_phrase = 0u;

	if (exe_compile_phrase & CAST8(CompilePhrase::kResolve))
		m_compiler->resolver.Resolve(this);
	if (exe_compile_phrase & CAST8(CompilePhrase::kCreateDescriptor))
		m_compiler->descriptor.Create(GetDevicePtr(), m_compiler->resolver);
	if (exe_compile_phrase & CAST8(CompilePhrase::kSchedule))
		m_compiler->scheduler.Schedule(m_compiler->resolver);
	if (exe_compile_phrase & CAST8(CompilePhrase::kAllocate))
		m_compiler->allocator.Allocate(GetDevicePtr(), m_compiler->resolver);
	if (exe_compile_phrase & CAST8(CompilePhrase::kPrepareExecutor))
		m_compiler->executor.Prepare(GetDevicePtr(), m_compiler->resolver, m_compiler->scheduler,
		                             m_compiler->allocator);
#undef CAST8
}

void RenderGraphBase::CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const {
	compile(); // Check & Compile before every execution
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
