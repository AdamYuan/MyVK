//
// Created by adamyuan on 2/4/24.
//

#include "Schedule.hpp"

#include <algorithm>

namespace default_executor {

Schedule Schedule::Create(const Args &args) {
	args.collection.ClearInfo(&PassInfo::schedule);

	Schedule s = {};
	fetch_render_areas(args);
	s.group_passes(args, merge_passes(args, make_image_read_graph(args)));
	return s;
}

void Schedule::fetch_render_areas(const Args &args) {
	auto graphics_pass_visitor = [&](const GraphicsPassBase *p_graphics_pass) {
		const auto &opt_area = p_graphics_pass->GetOptRenderArea();
		if (opt_area) {
			get_sched_info(p_graphics_pass).render_area =
			    std::visit(overloaded(
			                   [&](const std::invocable<VkExtent2D> auto &area_func) {
				                   return area_func(args.render_graph.GetCanvasSize());
			                   },
			                   [](const RenderPassArea &area) { return area; }),
			               *opt_area);
		} else {
			// Loop through Pass Inputs and find the largest attachment
			RenderPassArea area = {};
			for (const InputBase *p_input : Dependency::GetPassInputs(p_graphics_pass)) {
				if (!UsageIsAttachment(p_input->GetUsage()))
					continue;
				Dependency::GetInputResource(p_input)->Visit(overloaded(
				    [&](const ImageBase *p_image) {
					    const auto &size = ResourceMeta::GetViewInfo(p_image).size;
					    area.layers = std::max(area.layers, size.GetArrayLayers());
					    auto [width, height, _] = size.GetBaseMipExtent();
					    area.extent.width = std::max(area.extent.width, width);
					    area.extent.height = std::max(area.extent.height, height);
				    },
				    [](auto &&) {}));
			}
			get_sched_info(p_graphics_pass).render_area = area;
		}
	};
	for (const PassBase *p_pass : args.dependency.GetTopoIDPasses())
		p_pass->Visit(overloaded(graphics_pass_visitor, [](auto &&) {}));
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
			merge_sizes[i] = get_sched_info(p_pass).render_area == get_sched_info(p_prev_pass).render_area
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
			    e.opt_p_src_input->GetUsage() == e.p_dst_input->GetUsage())
				// Read operations with same Usage can also merge into the same RenderPass
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

void Schedule::group_passes(const Args &args, const std::vector<std::size_t> &merge_sizes) {
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

} // namespace default_executor