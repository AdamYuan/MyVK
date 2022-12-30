#ifdef MYVK_ENABLE_RG

#ifndef MYVK_RG_RENDER_GRAPH_HPP
#define MYVK_RG_RENDER_GRAPH_HPP

#include "myvk_rg/_details_/Pass.hpp"
#include "myvk_rg/_details_/RenderGraph.hpp"
#include "myvk_rg/_details_/Resource.hpp"

namespace myvk_rg {

using PoolKey = _details_::PoolKey;

using Usage = _details_::Usage;

using PassFlag = _details_::PassFlag;
template <typename Derived, uint8_t Flags, bool EnableResource = false>
using Pass = _details_::Pass<Derived, Flags, EnableResource>;
template <typename Derived, bool EnableResource = false>
using PassGroup = _details_::PassGroup<Derived, EnableResource>;

using Image = _details_::ImageBase;
using ManagedImage = _details_::ManagedImage;
using ExternalImageBase = _details_::ExternalImageBase;

using Buffer = _details_::BufferBase;
using ManagedBuffer = _details_::ManagedBuffer;
using ExternalBufferBase = _details_::ExternalBufferBase;

template <typename Derived> using RenderGraph = _details_::RenderGraph<Derived>;

} // namespace myvk_rg

#endif

#endif