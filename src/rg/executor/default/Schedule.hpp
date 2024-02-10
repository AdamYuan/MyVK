//
// Created by adamyuan on 2/4/24.
//

#pragma once
#ifndef MYVK_SCHEDULE_HPP
#define MYVK_SCHEDULE_HPP

#include "Dependency.hpp"
#include "ResourceMeta.hpp"

namespace default_executor {

class Schedule {
public:
	struct PassDependency {
		const ResourceBase *p_resource;
		std::vector<const InputBase *> src_s, dst_s;
	};
	struct SubpassDependency {
		const ImageBase *p_attachment;
		const InputBase *p_src, *p_dst;
	};
	struct PassGroup {
		std::vector<const PassBase *> p_subpasses;
		std::vector<SubpassDependency> subpass_deps;
		inline bool IsRenderPass() const { return p_subpasses[0]->GetType() == PassType::kGraphics; }
	};

private:
	struct Args {
		const RenderGraphBase &render_graph;
		const Collection &collection;
		const Dependency &dependency;
		const ResourceMeta &resource_meta;
	};

	std::vector<PassGroup> m_pass_groups;
	std::vector<PassDependency> m_pass_dependencies;

	static auto &get_sched_info(const PassBase *p_pass) { return GetPassInfo(p_pass).schedule; }

	static void fetch_render_areas(const Args &args);
	static Graph<const PassBase *, Dependency::PassEdge> make_extra_graph(const Args &args);
	std::vector<std::size_t> merge_passes(const Args &args);

public:
	static Schedule Create(const Args &args);
};

} // namespace default_executor

#endif // MYVK_SCHEDULE_HPP
