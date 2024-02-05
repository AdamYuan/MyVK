//
// Created by adamyuan on 2/5/24.
//

#pragma once
#ifndef MYVK_INFO_HPP
#define MYVK_INFO_HPP

#include <myvk_rg/interface/RenderGraph.hpp>

using namespace myvk_rg::interface;

struct PassInfo {
	struct {
		uint32_t topo_order{};
		friend class Dependency;
	} dependency;
};

struct ResourceInfo {};

inline PassInfo &GetPassInfo(const PassBase *p_pass) { return *p_pass->__GetPExecutorInfo<PassInfo>(); }
inline ResourceInfo &GetResourceInfo(const ResourceBase *p_resource) {
	return *p_resource->__GetPExecutorInfo<ResourceInfo>();
}

#endif // MYVK_INFO_HPP
