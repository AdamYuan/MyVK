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

	void tag_resources(const Args &args);
	void fetch_alloc_sizes(const Args &p_view_image);
	void fetch_alloc_usages(const Args &args);
	void propagate_meta(const Args &args);

	static auto &get_meta(const ResourceBase *p_resource) { return GetResourceInfo(p_resource).meta; }
	static auto &get_alloc(const ImageBase *p_image) { return get_meta(p_image).image_alloc; }
	static auto &get_alloc(const BufferBase *p_buffer) { return get_meta(p_buffer).buffer_alloc; }
	static auto &get_view(const ImageBase *p_image) { return get_meta(p_image).image_view; }
	static auto &get_view(const BufferBase *p_buffer) { return get_meta(p_buffer).buffer_view; }

public:
	static ResourceMeta Create(const Args &args);

	// Alloc ID (Internal & Local & Physical Resources)
	static std::size_t GetAllocID(const ResourceBase *p_resource) { return get_meta(p_resource).alloc_id; }
	inline std::size_t GetAllocCount() const { return m_alloc_id_resources.size(); }
	inline const ResourceBase *GetAllocIDResource(std::size_t alloc_id) const { return m_alloc_id_resources[alloc_id]; }
	inline const auto &GetAllocIDResources() const { return m_alloc_id_resources; }
	static const ResourceBase *GetAllocResource(const ResourceBase *p_resource) {
		return get_meta(p_resource).p_alloc_resource;
	}
	static const ImageBase *GetAllocResource(const ImageBase *p_image) {
		return static_cast<const ImageBase *>(get_meta(p_image).p_alloc_resource);
	}
	static const BufferBase *GetAllocResource(const BufferBase *p_buffer) {
		return static_cast<const BufferBase *>(get_meta(p_buffer).p_alloc_resource);
	}
	static bool IsAllocResource(const ResourceBase *p_resource) {
		return get_meta(p_resource).p_alloc_resource == p_resource;
	}
	static const auto &GetAllocInfo(const ImageBase *p_image) { return get_alloc(p_image); }
	static const auto &GetAllocInfo(const BufferBase *p_buffer) { return get_alloc(p_buffer); }

	// View ID (Internal & Local Resources)
	static std::size_t GetViewID(const ResourceBase *p_resource) { return get_meta(p_resource).view_id; }
	inline std::size_t GetViewCount() const { return m_view_id_resources.size(); }
	inline const ResourceBase *GetViewIDResource(std::size_t view_id) const { return m_view_id_resources[view_id]; }
	inline const auto &GetViewIDResources() const { return m_view_id_resources; }
	static const ResourceBase *GetViewResource(const ResourceBase *p_resource) {
		return get_meta(p_resource).p_view_resource;
	}
	static const ImageBase *GetViewResource(const ImageBase *p_image) {
		return static_cast<const ImageBase *>(get_meta(p_image).p_view_resource);
	}
	static const BufferBase *GetViewResource(const BufferBase *p_buffer) {
		return static_cast<const BufferBase *>(get_meta(p_buffer).p_view_resource);
	}
	static bool IsViewResource(const ResourceBase *p_resource) {
		return get_meta(p_resource).p_view_resource == p_resource;
	}

	static const auto &GetViewInfo(const ImageBase *p_image) { return get_view(p_image); }
	static const auto &GetViewInfo(const BufferBase *p_buffer) { return get_view(p_buffer); }
};

} // namespace default_executor

#endif
