//
// Created by adamyuan on 2/4/24.
//

#include "Schedule.hpp"

#include <algorithm>

namespace default_executor {

Schedule Schedule::Create(const Args &args) {
	args.collection.ClearInfo(&PassInfo::schedule);

	Schedule s = {};
	s.fetch_render_areas(args);
	s.merge_passes(args);
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

Graph<const PassBase *, Dependency::PassEdge> Schedule::make_extra_graph(const Schedule::Args &args) {
	// Add extra image read edges, since multiple reads to the same image can break merging if, for example the first
	// (topo_id) reads as Input attachment and the second reads as Sampler
	Graph<const PassBase *, Dependency::PassEdge> extra_graph;

	const auto &pass_graph = args.dependency.GetPassGraph();
	// Only process Local & Image access Edges
	auto view = pass_graph.MakeView(Dependency::kAnyFilter, [](const Dependency::PassEdge &e) -> bool {
		return e.type == Dependency::PassEdgeType::kLocal && e.p_resource->GetType() == ResourceType::kImage;
	});

	for (const PassBase *p_pass : view.GetVertices()) {
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
				extra_graph.AddEdge(pass_graph.GetToVertex(prev_edge_id), pass_graph.GetToVertex(edge_id),
				                    {.opt_p_src_input = prev_e.p_dst_input,
				                     .p_dst_input = e.p_dst_input,
				                     .p_resource = p_resource,
				                     .type = Dependency::PassEdgeType::kLocal});
			}
		}
	}
	return extra_graph;
}

std::vector<std::size_t> Schedule::merge_passes(const Args &args) {
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

	for (std::size_t i = 0; i < args.dependency.GetSortedPassCount(); ++i) {
		if (i == 0)
			continue;

		const PassBase *p_pass = args.dependency.GetTopoIDPass(i);
	}

	return merge_sizes;
}

} // namespace default_executor