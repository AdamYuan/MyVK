//
// Created by adamyuan on 2/10/24.
//

#pragma once
#ifndef MYVK_DEF_EXE_RESOURCEMETA_HPP
#define MYVK_DEF_EXE_RESOURCEMETA_HPP

#include "Dependency.hpp"

namespace default_executor {

class ResourceMeta {
private:
	struct Args {
		const RenderGraphBase &render_graph;
		const Collection &collection;
		const Dependency &dependency;
	};

	std::vector<const ResourceBase *> m_alloc_id_resources, m_view_id_resources;

	void tag_alloc_resources(const Args &args);
	void fetch_alloc_sizes(const Args &args);
	void fetch_alloc_usages(const Args &args);

	static auto &get_meta(const ResourceBase *p_resource) { return GetResourceInfo(p_resource).meta; }

public:
	static ResourceMeta Create(const Args &args);

	// Access Meta
	static const auto &GetMeta(const ResourceBase *p_resource) { return get_meta(p_resource); }

	// Alloc ID (Internal & Local & Physical Resources)
	static std::size_t GetResourceAllocID(const ResourceBase *p_resource) { return get_meta(p_resource).alloc_id; }
	inline std::size_t GetAllocResourceCount() const { return m_alloc_id_resources.size(); }
	inline const ResourceBase *GetAllocIDResource(std::size_t alloc_id) const { return m_alloc_id_resources[alloc_id]; }
	inline const auto &GetAllocIDResources() const { return m_alloc_id_resources; }
	static const ResourceBase *GetAllocResource(const ResourceBase *p_resource) {
		return get_meta(p_resource).p_alloc_resource;
	}
	static bool IsAllocResource(const ResourceBase *p_resource) {
		return get_meta(p_resource).p_alloc_resource == p_resource;
	}
	// View ID (Internal & Local Resources)
	static std::size_t GetResourceViewID(const ResourceBase *p_resource) { return get_meta(p_resource).view_id; }
	inline std::size_t GetViewResourceCount() const { return m_view_id_resources.size(); }
	inline const ResourceBase *GetViewIDResource(std::size_t view_id) const { return m_view_id_resources[view_id]; }
	inline const auto &GetViewIDResources() const { return m_view_id_resources; }
	static const ResourceBase *GetViewResource(const ResourceBase *p_resource) {
		return get_meta(p_resource).p_view_resource;
	}
	static bool IsViewResource(const ResourceBase *p_resource) {
		return get_meta(p_resource).p_view_resource == p_resource;
	}
};

} // namespace default_executor

#endif
