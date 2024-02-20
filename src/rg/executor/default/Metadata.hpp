//
// Created by adamyuan on 2/10/24.
//

#pragma once
#ifndef MYVK_DEF_EXE_RESOURCEMETA_HPP
#define MYVK_DEF_EXE_RESOURCEMETA_HPP

#include "Dependency.hpp"

namespace myvk_rg_executor {

class Metadata {
private:
	struct Args {
		const RenderGraphBase &render_graph;
		const Collection &collection;
		const Dependency &dependency;
	};

	void fetch_external_infos(const Args &args);
	void fetch_alloc_sizes(const Args &args);
	static void fetch_alloc_usages(const Args &args);
	static void propagate_alloc_info(const Args &args);
	static void fetch_render_areas(const Args &args);

	static auto &get_meta(const PassBase *p_pass) { return GetPassInfo(p_pass).metadata; }
	static auto &get_meta(const ResourceBase *p_resource) { return GetResourceInfo(p_resource).metadata; }
	static auto &get_alloc(const ImageBase *p_image) { return get_meta(p_image).image_alloc; }
	static auto &get_alloc(const BufferBase *p_buffer) { return get_meta(p_buffer).buffer_alloc; }
	static auto &get_view(const ImageBase *p_image) { return get_meta(p_image).image_view; }
	static auto &get_view(const BufferBase *p_buffer) { return get_meta(p_buffer).buffer_view; }

public:
	static Metadata Create(const Args &args);

	// Alloc Info (on Root resources)
	static const auto &GetAllocInfo(const ImageBase *p_image) { return get_alloc(p_image); }
	static const auto &GetAllocInfo(const BufferBase *p_buffer) { return get_alloc(p_buffer); }

	// View Info (on every resource)
	static const auto &GetViewInfo(const ImageBase *p_image) { return get_view(p_image); }
	static const auto &GetViewInfo(const BufferBase *p_buffer) { return get_view(p_buffer); }

	// Pass RenderArea
	static RenderPassArea GetPassRenderArea(const PassBase *p_pass) { return get_meta(p_pass).render_area; }
};

} // namespace myvk_rg_executor

#endif
