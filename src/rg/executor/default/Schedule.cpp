//
// Created by adamyuan on 2/4/24.
//

#include "Schedule.hpp"

#include <algorithm>
#include <cassert>

namespace default_executor {

Schedule Schedule::Create(const Args &args) {
	args.collection.ClearInfo(&PassInfo::schedule);

	Schedule s = {};
	s.make_pass_groups(args, merge_passes(args, make_image_read_graph(args)));
	s.make_barriers(args);
	return s;
}

Graph<const PassBase *, Dependency::PassEdge> Schedule::make_image_read_graph(const Schedule::Args &args) {
	// Add extra image read edges, since multiple reads to the same image can break merging if, for example the first
	// (topo_id) reads as Input attachment and the second reads as Sampler
	Graph<const PassBase *, Dependency::PassEdge> extra_graph;

	const auto &pass_graph = args.dependency.GetPassGraph();
	// Only process Local & Image access Edges
	auto view = pass_graph.MakeView(Dependency::kAnyFilter, [](const Dependency::PassEdge &e) -> bool {
		return e.p_resource->GetType() == ResourceType::kImage;
	});

	for (const PassBase *p_pass : view.GetVertices()) {
		extra_graph.AddVertex(p_pass);

		std::vector<std::size_t> out_edge_ids;
		for (auto [_, _1, edge_id] : view.GetOutEdges(p_pass))
			out_edge_ids.push_back(edge_id);

		// Sort Output Edges with Topological Order of its 'To' Vertex
		std::ranges::sort(out_edge_ids, [&](std::size_t l_edge_id, std::size_t r_edge_id) {
			const PassBase *p_l_pass = pass_graph.GetToVertex(l_edge_id), *p_r_pass = pass_graph.GetToVertex(r_edge_id);
			return Dependency::GetPassTopoID(p_l_pass) < Dependency::GetPassTopoID(p_r_pass);
		});

		std::unordered_map<const ResourceBase *, std::size_t> access_edges;

		for (std::size_t edge_id : out_edge_ids) {
			const auto &e = pass_graph.GetEdge(edge_id);
			const ResourceBase *p_resource = e.p_resource;

			auto it = access_edges.find(p_resource);
			if (it == access_edges.end())
				access_edges[p_resource] = edge_id;
			else {
				std::size_t prev_edge_id = it->second;
				it->second = edge_id;

				const auto &prev_e = pass_graph.GetEdge(prev_edge_id);
				extra_graph.AddEdge(
				    pass_graph.GetToVertex(prev_edge_id), pass_graph.GetToVertex(edge_id),
				    {.opt_p_src_input = prev_e.p_dst_input, .p_dst_input = e.p_dst_input, .p_resource = p_resource});
			}
		}
	}
	return extra_graph;
}

inline static bool is_image_read_grouped(const InputBase *p_l, const InputBase *p_r) {
	myvk_rg::Usage ul = p_l->GetUsage(), ur = p_r->GetUsage();
	return !UsageIsAttachment(ul) && !UsageIsAttachment(ur) && UsageGetImageLayout(ul) == UsageGetImageLayout(ur);
}

std::vector<std::size_t>
Schedule::merge_passes(const Args &args, const Graph<const PassBase *, Dependency::PassEdge> &image_read_pass_graph) {
	// Compute Merge Size

	// Calculate merge_size, Complexity: O(N + M)
	// merge_size == 0: The pass is not a graphics pass
	// merge_size == 1: The pass is a graphics pass, but can't be merged
	// merge_size >  1: The pass is a graphics pass, and it can be merged to a group of merge_size with the passes
	// before

	std::vector<std::size_t> merge_sizes(args.dependency.GetSortedPassCount());

	// Initial Merge Sizes
	merge_sizes[0] = args.dependency.GetTopoIDPass(0)->GetType() == PassType::kGraphics;
	for (std::size_t i = 1; i < args.dependency.GetSortedPassCount(); ++i) {
		const PassBase *p_pass = args.dependency.GetTopoIDPass(i);
		if (p_pass->GetType() == PassType::kGraphics) {
			// Both are RenderPass and have equal RenderArea
			const PassBase *p_prev_pass = args.dependency.GetTopoIDPass(i - 1);
			merge_sizes[i] = p_prev_pass->GetType() == PassType::kGraphics &&
			                         Metadata::GetPassRenderArea(p_pass) == Metadata::GetPassRenderArea(p_prev_pass)
			                     ? merge_sizes[i - 1] + 1
			                     : 1;
		} else
			merge_sizes[i] = 0;
	}

	// Exclude nullptr Pass
	const auto node_filter = [](const PassBase *p_pass) -> bool { return p_pass; };
	auto view = args.dependency.GetPassGraph().MakeView(node_filter, Dependency::kAnyFilter);
	auto image_read_view = image_read_pass_graph.MakeView(node_filter, Dependency::kAnyFilter);

	for (std::size_t i = 0; i < args.dependency.GetSortedPassCount(); ++i) {
		if (i == 0)
			continue;

		const PassBase *p_pass = args.dependency.GetTopoIDPass(i);
		std::size_t &size = merge_sizes[i];

		for (auto [from, e, _] : view.GetInEdges(p_pass)) {
			std::size_t from_topo_id = Dependency::GetPassTopoID(from);
			if (UsageIsAttachment(e.opt_p_src_input->GetUsage()) && UsageIsAttachment(e.p_dst_input->GetUsage()))
				size = std::min(size, i - from_topo_id + merge_sizes[from_topo_id]);
			else
				size = std::min(size, i - from_topo_id);
		}
		for (auto [from, e, _] : image_read_view.GetInEdges(p_pass)) {
			std::size_t from_topo_id = Dependency::GetPassTopoID(from);
			if (UsageIsAttachment(e.opt_p_src_input->GetUsage()) && UsageIsAttachment(e.p_dst_input->GetUsage()) ||
			    is_image_read_grouped(e.opt_p_src_input, e.p_dst_input))
				// Image Reads with same Usage can merge into the same RenderPass
				size = std::min(size, i - from_topo_id + merge_sizes[from_topo_id]);
			else
				size = std::min(size, i - from_topo_id);
		}
	}

	// Regularize Merge Sizes
	for (std::size_t i = 0, prev_size = 0; i < args.dependency.GetSortedPassCount(); ++i) {
		const PassBase *p_pass = args.dependency.GetTopoIDPass(i);
		auto &size = merge_sizes[i];
		if (size > prev_size)
			size = prev_size + 1;
		else
			size = p_pass->GetType() == PassType::kGraphics ? 1 : 0;

		prev_size = size;
	}

	return merge_sizes;
}

void Schedule::make_pass_groups(const Args &args, const std::vector<std::size_t> &merge_sizes) {
	for (std::size_t topo_id = 0; const PassBase *p_pass : args.dependency.GetTopoIDPasses()) {
		std::size_t merge_size = merge_sizes[topo_id];
		if (merge_size <= 1) {
			get_sched_info(p_pass).group_id = m_pass_groups.size();
			get_sched_info(p_pass).subpass_id = 0;
			m_pass_groups.emplace_back();
		} else {
			get_sched_info(p_pass).group_id = m_pass_groups.size() - 1;
			get_sched_info(p_pass).subpass_id = merge_size - 1;
		}
		m_pass_groups.back().subpasses.push_back(p_pass);

		++topo_id;
	}
}

Schedule::BarrierType Schedule::get_valid_barrier_type(const ResourceBase *p_valid_resource) {
	switch (p_valid_resource->GetState()) {
	case ResourceState::kExternal:
		return BarrierType::kExtValid;
	case ResourceState::kLastFrame:
		return BarrierType::kLFValid;
	default:
		return BarrierType::kValid;
	}
}

void Schedule::for_each_read_group(const ResourceBase *p_resource, std::vector<const InputBase *> &&reads,
                                   auto &&func) {
	if (reads.empty())
		return;

	std::ranges::sort(reads, [&](auto l_read, auto r_read) {
		return Dependency::GetPassTopoID(Dependency::GetInputPass(l_read)) <
		       Dependency::GetPassTopoID(Dependency::GetInputPass(r_read));
	});

	if (p_resource->GetType() == ResourceType::kBuffer) {
		func(std::span<const InputBase *const>{}, reads); // Read-After-Write Barrier
		func(reads, std::span<const InputBase *const>{}); // Write-After-Read Barrier
	} else {
		std::span<const InputBase *const> prev_span{};
		for (std::size_t i = 0; i < reads.size();) {
			std::size_t j = i + 1;
			while (j < reads.size() && is_image_read_grouped(reads[j - 1], reads[j]))
				++j;

			std::span<const InputBase *const> span = {reads.data() + i, reads.data() + j};
			func(prev_span, span);
			prev_span = span;

			i = j;
		}
		func(prev_span, std::span<const InputBase *const>{});
	}
}

void Schedule::push_read_barrier(const ResourceBase *p_resource, const InputBase *p_write,
                                 const InputBase *p_next_write, Schedule::BarrierType raw_barrier_type,
                                 std::span<const InputBase *const> src_s, std::span<const InputBase *const> dst_s) {
	if (src_s.empty() && p_write)
		src_s = std::span<const InputBase *const>{&p_write, 1};
	if (dst_s.empty() && p_next_write)
		dst_s = std::span<const InputBase *const>{&p_next_write, 1};

	if (std::size_t group;
	    !src_s.empty() && !dst_s.empty() &&
	    (group = GetGroupID(Dependency::GetInputPass(src_s[0]))) == GetGroupID(Dependency::GetInputPass(dst_s[0]))) {
		// Subpass Barrier
		assert(src_s.size() == 1 && dst_s.size() == 1);
		assert(UsageIsAttachment(src_s[0]->GetUsage()) && UsageIsAttachment(dst_s[0]->GetUsage()));
		m_pass_groups[group].subpass_deps.push_back(SubpassBarrier{
		    .p_attachment = static_cast<const ImageBase *>(p_resource),
		    .p_src = src_s[0],
		    .p_dst = dst_s[0],
		});
	} else if (!dst_s.empty()) {
		// Ignore Write-after-Read barriers when p_next_write is null
		m_pass_barriers.push_back({.p_resource = p_resource,
		                           .src_s = std::vector<const InputBase *>{src_s.begin(), src_s.end()},
		                           .dst_s = std::vector<const InputBase *>{dst_s.begin(), dst_s.end()},
		                           .type = src_s.empty() ? raw_barrier_type : BarrierType::kLocal});
	}
}

namespace make_barriers {
struct ReadInfo {
	std::vector<const InputBase *> reads;
	const InputBase *p_next_write;
};
} // namespace make_barriers
void Schedule::make_barriers(const Schedule::Args &args) {
	using make_barriers::ReadInfo;

	std::unordered_map<const InputBase *, ReadInfo> local_reads;
	std::unordered_map<const ResourceBase *, ReadInfo> valid_reads;

	// Write-After-Write or Write validation Barriers are directly added, WAR and RAW Barriers are remained to be
	// processed (push to local_reads and valid_reads firstly)
	for (auto [from, to, e, _] : args.dependency.GetPassGraph().GetEdges()) {
		if (from == nullptr) {
			// Validation
			auto [_, p_dst_input, p_resource] = e;
			// Not Local Dependency, should be Valid / LFValid / ExtValid
			bool dst_write = !UsageIsReadOnly(p_dst_input->GetUsage());
			if (dst_write) {
				// Write Validation, push directly
				m_pass_barriers.push_back({
				    .p_resource = p_resource,
				    .src_s = {},
				    .dst_s = {p_dst_input},
				    .type = get_valid_barrier_type(p_resource),
				});
			} else {
				// Read Validation, push to Valid Read Edges
				valid_reads[p_resource].reads.push_back(p_dst_input);
			}
		} else {
			auto [p_src_input, p_dst_input, p_resource] = e;

			bool src_write = !UsageIsReadOnly(p_src_input->GetUsage()),
			     dst_write = !UsageIsReadOnly(p_dst_input->GetUsage());
			assert(src_write || dst_write); // Should have a write access

			if (src_write && dst_write) {
				// Both are write access, push directly
				if (GetGroupID(from) == GetGroupID(to)) {
					m_pass_groups[GetGroupID(from)].subpass_deps.push_back(SubpassBarrier{
					    .p_attachment = static_cast<const ImageBase *>(p_resource),
					    .p_src = p_src_input,
					    .p_dst = p_dst_input,
					});
					assert(p_resource->GetType() == ResourceType::kImage &&
					       UsageIsAttachment(e.opt_p_src_input->GetUsage()) &&
					       UsageIsAttachment(e.p_dst_input->GetUsage()));
				} else {
					m_pass_barriers.push_back({
					    .p_resource = e.p_resource,
					    .src_s = {p_src_input},
					    .dst_s = {p_dst_input},
					    .type = BarrierType::kLocal,
					});
				}
			} else if (src_write) {
				// Read after Write, push to RAW edges
				local_reads[p_src_input].reads.push_back(p_dst_input);
			}
		}
	}

	// Fetch p_next_write from Indirect WAW Graph
	for (auto [from, to, e, _] : args.dependency.GetPassIndirectWAWGraph().GetEdges()) {
		ReadInfo &info = from == nullptr ? valid_reads[e.p_resource] : local_reads[e.opt_p_src_input];
		info.p_next_write = e.p_dst_input;
	}

	for (auto &[p_input, read_info] : local_reads) {
		const ResourceBase *p_resource = Dependency::GetInputResource(p_input);
		const InputBase *p_next_write = read_info.p_next_write;

		for_each_read_group(p_resource, std::move(read_info.reads), [&](auto src_s, auto dst_s) {
			push_read_barrier(p_resource, p_input, p_next_write, BarrierType::kLocal, src_s, dst_s);
		});
	}
	for (auto &[p_resource, read_info] : valid_reads) {
		const InputBase *p_next_write = read_info.p_next_write;
		BarrierType barrier_type = get_valid_barrier_type(p_resource);
		for_each_read_group(p_resource, std::move(read_info.reads), [&](auto src_s, auto dst_s) {
			push_read_barrier(p_resource, nullptr, p_next_write, barrier_type, src_s, dst_s);
		});
	}
}

} // namespace default_executor