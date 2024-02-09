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

	std::vector<const ResourceBase *> m_alloc_id_resources, m_view_id_resources;

	static auto &get_alloc_info(const ResourceBase *p_resource) { return GetResourceInfo(p_resource).allocation; }

	void tag_alloc_resources(const Args &args);
	void fetch_alloc_sizes(const Args &args);
	void fetch_alloc_usages(const Args &args);
	void create_vk_resources();
	void create_vk_image_views();

public:
	static Allocation Create(const myvk::Ptr<myvk::Device> &device_ptr, const Args &args);

	// Alloc ID (Internal Local Root Resources)
	static std::size_t GetResourceAllocID(const ResourceBase *p_resource) {
		return get_alloc_info(p_resource).alloc_id;
	}
	const ResourceBase *GetAllocIDResource(std::size_t alloc_id) const { return m_alloc_id_resources[alloc_id]; }
	static const ResourceBase *GetAllocResource(const ResourceBase *p_resource) {
		return get_alloc_info(p_resource).p_alloc_resource;
	}
	static bool IsAllocResource(const ResourceBase *p_resource) {
		return get_alloc_info(p_resource).p_alloc_resource == p_resource;
	}

	// View ID (Internal Local Resources)
	static std::size_t GetResourceViewID(const ResourceBase *p_resource) { return get_alloc_info(p_resource).view_id; }
	const ResourceBase *GetViewIDResource(std::size_t view_id) const { return m_view_id_resources[view_id]; }
	static const ResourceBase *GetViewResource(const ResourceBase *p_resource) {
		return get_alloc_info(p_resource).p_view_resource;
	}
	static bool IsViewResource(const ResourceBase *p_resource) {
		return get_alloc_info(p_resource).p_view_resource == p_resource;
	}
};

} // namespace default_executor

#endif // MYVK_RG_EXE_DEF_ALLOCATOR_HPP
