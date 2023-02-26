#ifdef MYVK_ENABLE_RG

#ifndef MYVK_RG_RENDER_GRAPH_HPP
#define MYVK_RG_RENDER_GRAPH_HPP

#include "_details_/RenderGraph.hpp"

namespace myvk_rg {

using Key = _details_::PoolKey;

template <typename Derived> using GraphicsPass = _details_::GraphicsPass<Derived>;
template <typename Derived> using ComputePass = _details_::ComputePass<Derived>;
template <typename Derived> using TransferPass = _details_::TransferPass<Derived>;
template <typename Derived> using PassGroup = _details_::PassGroup<Derived>;

using ImageInput = const _details_::ImageBase *;
using ImageOutput = const _details_::ImageAlias *;
using ManagedImage = _details_::ManagedImage;
using ExternalImageBase = _details_::ExternalImageBase;
#ifdef MYVK_ENABLE_GLFW
using SwapchainImage = _details_::SwapchainImage;
#endif

using BufferInput = const _details_::BufferBase *;
using BufferOutput = const _details_::BufferAlias *;
using ManagedBuffer = _details_::ManagedBuffer;
using ExternalBufferBase = _details_::ExternalBufferBase;

template <typename Derived> using RenderGraph = _details_::RenderGraph<Derived>;

} // namespace myvk_rg

#endif

#endif