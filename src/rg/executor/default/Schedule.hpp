//
// Created by adamyuan on 2/4/24.
//

#pragma once
#ifndef MYVK_SCHEDULE_HPP
#define MYVK_SCHEDULE_HPP

#include "Dependency.hpp"
#include "Metadata.hpp"

namespace default_executor {

class Schedule {
public:
	enum class BarrierType { kLocal, kValid, kLFValid, kExtValid, kExtOut };
	struct PassBarrier {
		const ResourceBase *p_resource;
		std::vector<const InputBase *> src_s, dst_s;
		BarrierType type;
	};
	struct SubpassBarrier {
		const ImageBase *p_attachment;
		const InputBase *p_src, *p_dst;
	};
	struct PassGroup {
		std::vector<const PassBase *> subpasses;
		std::vector<SubpassBarrier> subpass_deps;
		inline bool IsRenderPass() const { return subpasses[0]->GetType() == PassType::kGraphics; }
	};

private:
	struct Args {
		const RenderGraphBase &render_graph;
		const Collection &collection;
		const Dependency &dependency;
		const Metadata &metadata;
	};

	std::vector<PassGroup> m_pass_groups;
	std::vector<PassBarrier> m_pass_barriers;

	static auto &get_sched_info(const PassBase *p_pass) { return GetPassInfo(p_pass).schedule; }

	static BarrierType get_valid_barrier_type(const ResourceBase *p_valid_resource);

	static Graph<const PassBase *, Dependency::PassEdge> make_image_read_graph(const Schedule::Args &args);
	static std::vector<std::size_t>
	merge_passes(const Args &args, const Graph<const PassBase *, Dependency::PassEdge> &image_read_pass_graph);
	void make_pass_groups(const Args &args, const std::vector<std::size_t> &merge_sizes);
	static void for_each_read_group(const ResourceBase *p_resource, std::vector<const InputBase *> &&reads,
	                                auto &&func);
	void push_read_barrier(const ResourceBase *p_resource, const InputBase *p_write, const InputBase *p_next_write,
	                       Schedule::BarrierType raw_barrier_type, std::span<const InputBase *const> src_s,
	                        std::span<const InputBase *const> dst_s);
	void make_barriers(const Args &args);

public:
	static Schedule Create(const Args &args);
	inline const auto &GetPassGroups() const { return m_pass_groups; }
	static std::size_t GetGroupID(const PassBase *p_pass) { return get_sched_info(p_pass).group_id; }
	static std::size_t GetSubpassID(const PassBase *p_pass) { return get_sched_info(p_pass).subpass_id; }
};

} // namespace default_executor

#endif // MYVK_SCHEDULE_HPP
