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
	Relation m_resource_alias_relation;

	static auto &get_alloc_info(const ResourceBase *p_resource) { return GetResourceInfo(p_resource).allocation; }

	void tag_alloc_resources(const Args &args);
	void fetch_alloc_sizes(const Args &args);
	void fetch_alloc_usages(const Args &args);
	void create_vk_resources();
	static std::tuple<VkDeviceSize, uint32_t> fetch_memory_requirements(std::ranges::input_range auto &&resources);
	void alloc_naive(std::ranges::input_range auto &&resources, const VmaAllocationCreateInfo &create_info);
	void alloc_optimal(const Args &args, std::ranges::input_range auto &&resources,
	                   const VmaAllocationCreateInfo &create_info);
	void create_vk_allocations(const Args &args);
	void bind_vk_resources();
	void create_vk_image_views();

public:
	static Allocation Create(const myvk::Ptr<myvk::Device> &device_ptr, const Args &args);

	// Alloc ID (Internal & Local & Physical Resources)
	static std::size_t GetResourceAllocID(const ResourceBase *p_resource) {
		return get_alloc_info(p_resource).alloc_id;
	}
	inline const ResourceBase *GetAllocIDResource(std::size_t alloc_id) const { return m_alloc_id_resources[alloc_id]; }
	static const ResourceBase *GetAllocResource(const ResourceBase *p_resource) {
		return get_alloc_info(p_resource).p_alloc_resource;
	}
	static bool IsAllocResource(const ResourceBase *p_resource) {
		return get_alloc_info(p_resource).p_alloc_resource == p_resource;
	}
	// Resource Alias Relationship
	inline bool IsResourceAliased(std::size_t alloc_id_l, std::size_t alloc_id_r) const {
		return m_resource_alias_relation.Get(alloc_id_l, alloc_id_r);
	}
	inline bool IsResourceAliased(const ResourceBase *p_l, const ResourceBase *p_r) const {
		return IsResourceAliased(GetResourceAllocID(p_l), GetResourceAllocID(p_r));
	}

	// View ID (Internal & Local Resources)
	static std::size_t GetResourceViewID(const ResourceBase *p_resource) { return get_alloc_info(p_resource).view_id; }
	inline const ResourceBase *GetViewIDResource(std::size_t view_id) const { return m_view_id_resources[view_id]; }
	static const ResourceBase *GetViewResource(const ResourceBase *p_resource) {
		return get_alloc_info(p_resource).p_view_resource;
	}
	static bool IsViewResource(const ResourceBase *p_resource) {
		return get_alloc_info(p_resource).p_view_resource == p_resource;
	}
};

} // namespace default_executor

#endif // MYVK_RG_EXE_DEF_ALLOCATOR_HPP
