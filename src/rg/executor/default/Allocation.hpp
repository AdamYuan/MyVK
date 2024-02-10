//
// Created by adamyuan on 2/8/24.
//

#pragma once
#ifndef MYVK_RG_EXE_DEF_ALLOCATOR_HPP
#define MYVK_RG_EXE_DEF_ALLOCATOR_HPP

#include "ResourceMeta.hpp"

#include <myvk/Device.hpp>

namespace default_executor {

class Allocation {
private:
	struct Args {
		const RenderGraphBase &render_graph;
		const Collection &collection;
		const Dependency &dependency;
		const ResourceMeta &resource_meta;
	};

	myvk::Ptr<myvk::Device> m_device_ptr;

	Relation m_resource_alias_relation;

	static auto &get_vk_alloc(const ResourceBase *p_resource) { return GetResourceInfo(p_resource).allocation; }

	void create_vk_resources(const Args &args);
	static std::tuple<VkDeviceSize, uint32_t> fetch_memory_requirements(std::ranges::input_range auto &&resources);
	void alloc_naive(std::ranges::input_range auto &&resources, const VmaAllocationCreateInfo &create_info);
	void alloc_optimal(const Args &args, std::ranges::input_range auto &&resources,
	                   const VmaAllocationCreateInfo &create_info);
	void create_vk_allocations(const Args &args);
	void bind_vk_resources(const Args &args);
	void create_vk_image_views(const Args &args);

public:
	static Allocation Create(const myvk::Ptr<myvk::Device> &device_ptr, const Args &args);

	// Resource Alias Relationship
	inline bool IsResourceAliased(std::size_t alloc_id_l, std::size_t alloc_id_r) const {
		return m_resource_alias_relation.Get(alloc_id_l, alloc_id_r);
	}
	inline bool IsResourceAliased(const ResourceBase *p_l, const ResourceBase *p_r) const {
		return IsResourceAliased(ResourceMeta::GetAllocID(p_l), ResourceMeta::GetAllocID(p_r));
	}
};

} // namespace default_executor

#endif // MYVK_RG_EXE_DEF_ALLOCATOR_HPP
