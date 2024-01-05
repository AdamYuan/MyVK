#ifdef MYVK_ENABLE_RG

#ifndef MYVK_RG_RENDER_GRAPH_HPP
#define MYVK_RG_RENDER_GRAPH_HPP

#include "interface/RenderGraph.hpp"

namespace myvk_rg {

using Key = interface::PoolKey;

using GraphicsPassBase = interface::GraphicsPassBase;
using ComputePassBase = interface::ComputePassBase;
using TransferPassBase = interface::TransferPassBase;
using PassGroupBase = interface::PassGroupBase;

using ImageBase = interface::ImageBase;
using ImageInput = const interface::ImageBase *;
using ImageOutput = const interface::ImageAliasBase *;
using ManagedImage = interface::ManagedImage;
using CombinedImage = interface::CombinedImage;
using LastFrameImage = interface::LastFrameImage;
using ExternalImageBase = interface::ExternalImageBase;

using BufferBase = interface::BufferBase;
using BufferInput = const interface::BufferBase *;
using BufferOutput = const interface::BufferAliasBase *;
using ManagedBuffer = interface::ManagedBuffer;
using LastFrameBuffer = interface::LastFrameBuffer;
using ExternalBufferBase = interface::ExternalBufferBase;

using SubImageSize = interface::SubImageSize;
using RenderPassArea = interface::RenderPassArea;

using BufferDescriptorInput = interface::BufferDescriptorInput;
using ImageDescriptorInput = interface::ImageDescriptorInput;
using SamplerDescriptorInput = interface::SamplerDescriptorInput;

template <typename Derived> using RenderGraph = interface::RenderGraph<Derived>;

} // namespace myvk_rg

#endif

#endif