//
// Created by adamyuan on 2/5/24.
//

#pragma once
#ifndef MYVK_INFO_HPP
#define MYVK_INFO_HPP

#include <myvk_rg/interface/RenderGraph.hpp>
#include "../Bitset.hpp"

namespace default_executor {

using namespace myvk_rg::interface;
using namespace myvk_rg::executor;

struct PassInfo {
	struct {
		std::size_t topo_id{};
		friend class Dependency;
	} dependency;
};

struct ResourceInfo {
	struct {
		std::size_t phys_id{};
		const ResourceBase *p_root_resource{}, *p_lf_resource{};
		Bitset access_passes;
		friend class Dependency;
	} dependency;
};

inline PassInfo &GetPassInfo(const PassBase *p_pass) { return *p_pass->__GetPExecutorInfo<PassInfo>(); }
inline ResourceInfo &GetResourceInfo(const ResourceBase *p_resource) {
	return *p_resource->__GetPExecutorInfo<ResourceInfo>();
}

} // namespace default_executor

#endif // MYVK_INFO_HPP
