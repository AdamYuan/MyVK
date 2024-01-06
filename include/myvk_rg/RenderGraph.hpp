#ifdef MYVK_ENABLE_RG

#ifndef MYVK_RG_RENDER_GRAPH_HPP
#define MYVK_RG_RENDER_GRAPH_HPP

#include "interface/RenderGraph.hpp"

namespace myvk_rg {

using Key = interface::PoolKey;
using Parent = interface::Parent;

using GraphicsPassBase = interface::GraphicsPassBase;
using ComputePassBase = interface::ComputePassBase;
using TransferPassBase = interface::TransferPassBase;
using PassGroupBase = interface::PassGroupBase;

using Image = interface::ImageAliasBase;
using ImageOutput = interface::OutputImageAlias;
using ManagedImage = interface::ManagedImage;
using CombinedImage = interface::CombinedImage;
using LastFrameImage = interface::LastFrameImage;
using ExternalImageBase = interface::ExternalImageBase;

using Buffer = interface::BufferAliasBase;
using BufferOutput = interface::OutputBufferAlias;
using ManagedBuffer = interface::ManagedBuffer;
using LastFrameBuffer = interface::LastFrameBuffer;
using ExternalBufferBase = interface::ExternalBufferBase;

using SubImageSize = interface::SubImageSize;
using RenderPassArea = interface::RenderPassArea;

using BufferDescriptorInput = interface::BufferDescriptorInput;
using ImageDescriptorInput = interface::ImageDescriptorInput;
using SamplerDescriptorInput = interface::SamplerDescriptorInput;

template <typename Executor = executor::DefaultExecutor> class RenderGraphBase : public interface::RenderGraphBase {
private:
	inline static const interface::PoolKey kEXEKey = {"[EXE]"};

public:
	inline explicit RenderGraphBase()
	    : interface::RenderGraphBase(
	          myvk::MakeUPtr<Executor>(Parent{.p_pool_key = &kEXEKey, .p_var_parent = (RenderGraphBase *)this})) {}
	inline ~RenderGraphBase() override = default;
	inline const Executor *GetExecutor() const {
		return static_cast<const Executor *>(interface::RenderGraphBase::GetExecutor());
	}
};

} // namespace myvk_rg

#endif

#endif