//
// Created by adamyuan on 2/8/24.
//

#pragma once
#ifndef MYVK_RG_EXE_DEF_ALLOCATOR_HPP
#define MYVK_RG_EXE_DEF_ALLOCATOR_HPP

#include "Dependency.hpp"

#include <myvk/Device.hpp>

namespace default_executor {

class Allocation {
private:
	struct Args {
		const RenderGraphBase &render_graph;
		const Collection &collection;
		const Dependency &dependency;
	};

	myvk::Ptr<myvk::Device> m_device_ptr;

	static auto &get_buffer_alloc(const ResourceBase *p_resource) {
		return GetResourceInfo(p_resource).buffer_allocation;
	}
	static auto &get_image_alloc(const ResourceBase *p_resource) {
		return GetResourceInfo(p_resource).image_allocation;
	}

	CompileResult<void> fetch_alloc_sizes(const Args &args);

public:
	static CompileResult<Allocation> Create(const myvk::Ptr<myvk::Device> &device_ptr, const Args &args);
};

} // namespace default_executor

#endif // MYVK_RG_EXE_DEF_ALLOCATOR_HPP
