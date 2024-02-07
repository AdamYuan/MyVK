#include "Dependency.hpp"

namespace default_executor {

CompileResult<Dependency> Dependency::Create(const Args &args) {
	Dependency g = {};

	for (const auto &it : args.render_graph.GetResultPoolData()) {
		UNWRAP(it.second.Visit([&](const auto *p_alias) -> CompileResult<void> {
			const PassBase *p_pass;
			UNWRAP_ASSIGN(p_pass, args.collection.FindPass(p_alias->GetSourcePassKey()));
			UNWRAP(g.traverse_pass(args, p_pass));
			return {};
		}));
	}
	UNWRAP(g.add_war_edges());
	UNWRAP(g.sort_passes());
	UNWRAP(g.get_pass_relation());

	UNWRAP(g.tag_resources());

	return g;
}

CompileResult<void> Dependency::traverse_pass(const Args &args, const PassBase *p_pass) {
	if (m_pass_graph.HasVertex(p_pass))
		return {};
	const auto pass_visitor = overloaded(
	    [&](const PassWithInput auto *p_pass) -> CompileResult<void> {
		    for (const auto &it : p_pass->GetInputPoolData()) {
			    const InputBase *p_input = it.second.template Get<InputBase>();

			    UNWRAP(p_input->GetInputAlias().Visit(overloaded(
			        [&](const OutputAlias auto *p_output_alias) -> CompileResult<void> {
				        const PassBase *p_src_pass;
				        UNWRAP_ASSIGN(p_src_pass, args.collection.FindPass(p_output_alias->GetSourcePassKey()));
				        const InputBase *p_src_input;
				        UNWRAP_ASSIGN(p_src_input, args.collection.FindInput(p_output_alias->GetSourceKey()));

				        UNWRAP(traverse_pass(args, p_src_pass));
				        const ResourceBase *p_resource = m_input_2_resource.at(p_src_input);
				        m_input_2_resource[p_input] = p_resource;

				        m_pass_graph.AddEdge(p_src_pass, p_pass,
				                             PassEdge{p_src_input, p_input, p_resource, PassEdgeType::kLocal});

				        return {};
			        },
			        [&](const RawAlias auto *p_raw_alias) -> CompileResult<void> {
				        const ResourceBase *p_resource;
				        UNWRAP_ASSIGN(p_resource, args.collection.FindResource(p_raw_alias->GetSourceKey()));
				        m_resource_graph.AddVertex(p_resource);
				        m_input_2_resource[p_input] = p_resource;

				        UNWRAP(p_resource->Visit(overloaded(
				            [&](const CombinedImage *p_combined_image) -> CompileResult<void> {
					            for (const OutputImageAlias &src_alias : p_combined_image->GetSubImages()) {
						            const PassBase *p_src_pass;
						            UNWRAP_ASSIGN(p_src_pass, args.collection.FindPass(src_alias.GetSourcePassKey()));
						            const InputBase *p_src_input;
						            UNWRAP_ASSIGN(p_src_input, args.collection.FindInput(src_alias.GetSourceKey()));

						            UNWRAP(traverse_pass(args, p_src_pass));
						            const ResourceBase *p_sub_image = m_input_2_resource.at(p_src_input);

						            m_pass_graph.AddEdge(
						                p_src_pass, p_pass,
						                PassEdge{p_src_input, p_input, p_sub_image, PassEdgeType::kLocal});
						            m_resource_graph.AddEdge(p_resource, p_sub_image, {ResourceEdgeType::kSubResource});
					            }
					            return {};
				            },
				            [&](const LastFrameResource auto *p_lf_resource) -> CompileResult<void> {
					            m_pass_graph.AddEdge(nullptr, p_pass,
					                                 PassEdge{nullptr, p_input, p_resource, PassEdgeType::kLocal});

					            const auto &src_alias = p_lf_resource->GetPointedAlias();
					            const PassBase *p_src_pass;
					            UNWRAP_ASSIGN(p_src_pass, args.collection.FindPass(src_alias.GetSourcePassKey()));
					            const InputBase *p_src_input;
					            UNWRAP_ASSIGN(p_src_input, args.collection.FindInput(src_alias.GetSourceKey()));

					            UNWRAP(traverse_pass(args, p_src_pass));
					            const ResourceBase *p_src_resource = m_input_2_resource.at(p_src_input);

					            // Last Frame Edges
					            m_pass_graph.AddEdge(
					                p_src_pass, p_pass,
					                PassEdge{p_src_input, p_input, p_src_resource, PassEdgeType::kLastFrame});
					            m_resource_graph.AddEdge(p_resource, p_src_resource, {ResourceEdgeType::kLastFrame});
					            return {};
				            },
				            [&](auto &&) -> CompileResult<void> {
					            m_pass_graph.AddEdge(nullptr, p_pass,
					                                 PassEdge{nullptr, p_input, p_resource, PassEdgeType::kLocal});
					            return {};
				            })));

				        return {};
			        })));
		    }
		    return {};
	    },
	    [](const auto *) -> CompileResult<void> { return {}; });

	UNWRAP(p_pass->Visit(pass_visitor));
	return {};
}

struct AccessEdgeInfo {
	std::vector<std::size_t> reads;
	std::optional<std::size_t> opt_write;
};
CompileResult<void> Dependency::add_war_edges() {
	auto view = m_pass_graph.MakeView(kAnyFilter, kPassEdgeFilter<PassEdgeType::kLocal>);

	for (const PassBase *p_pass : view.GetVertices()) {
		std::unordered_map<const ResourceBase *, AccessEdgeInfo> access_edges;

		for (auto [_, e, edge_id] : view.GetOutEdges(p_pass)) {
			auto &info = access_edges[e.p_resource];
			if (UsageIsReadOnly(e.p_dst_input->GetUsage()))
				info.reads.push_back(edge_id);
			else {
				// Forbid multiple writes
				if (info.opt_write)
					return error::MultipleWrite{.alias = e.p_dst_input->GetInputAlias()};
				// Forbid writes to last frame resource
				if (e.p_resource->GetState() == myvk_rg::interface::ResourceState::kLastFrame)
					return error::WriteToLastFrame{.alias = e.p_dst_input->GetInputAlias(),
					                               .pass_key = p_pass->GetGlobalKey()};

				info.opt_write = edge_id;
			}
		}

		for (const auto &[p_resource, info] : access_edges) {
			// If no writes or no reads, skip
			if (!info.opt_write || info.reads.empty())
				continue;

			std::size_t write_id = *info.opt_write;

			// Remove direct write edge
			m_pass_graph.RemoveEdge(write_id);

			// Add edges from read to write
			for (std::size_t read_id : info.reads) {
				m_pass_graph.AddEdge(m_pass_graph.GetToVertex(read_id), m_pass_graph.GetToVertex(write_id),
				                     PassEdge{
				                         .opt_p_src_input = m_pass_graph.GetEdge(read_id).p_dst_input,
				                         .p_dst_input = m_pass_graph.GetEdge(write_id).p_dst_input,
				                         .p_resource = p_resource,
				                         .type = PassEdgeType::kLocal,
				                     });
			}
		}
	}
	return {};
}

CompileResult<void> Dependency::sort_passes() {
	// Exclude nullptr Pass, use Local edges only
	auto view = m_pass_graph.MakeView([](const PassBase *p_pass) -> bool { return p_pass; },
	                                  kPassEdgeFilter<PassEdgeType::kLocal>);

	auto kahn_result = view.KahnTopologicalSort();

	if (!kahn_result.is_dag)
		return error::PassCycleExist{};

	// Assign topo-order to passes
	m_topo_order_passes = std::move(kahn_result.sorted);
	for (std::size_t topo_order = 0; const PassBase *p_pass : m_topo_order_passes)
		GetPassInfo(p_pass).dependency.topo_order = topo_order++;

	return {};
}

CompileResult<void> Dependency::tag_resources() {
	/* auto find_trees_result = m_resource_graph.FindTrees(
	    kResourceEdgeFilter<ResourceEdgeType::kSubResource>, ) for (const ResourceBase *p_resource :
	                                                                m_resource_graph.GetVertices()) {
		std::size_t in_edge_count = 0, sub_resource_in_edge_count = 0;

		{ // Get Input Edge Counts
			auto in_edges = m_resource_graph.GetInEdges(p_resource);
			for (auto [_, e, _1] : in_edges) {
				++in_edge_count;
				sub_resource_in_edge_count += e.type == ResourceEdgeType::kSubResource;
			}
		}

		{ // Validate
			// Resource should not be shared by multiple CombinedImage
			if (sub_resource_in_edge_count > 1)
				return error::ResourceMultiParent{.key = p_resource->GetGlobalKey()};

			// LastFrame Resource should not have parent resource
			if (p_resource->GetState() == myvk_rg::interface::ResourceState::kLastFrame && in_edge_count)
				return error::ResourceLFParent{.key = p_resource->GetGlobalKey()};
		}

		// Assign Physical ID
		if (sub_resource_in_edge_count == 0) {
			GetResourceInfo(p_resource).dependency.phys_id = m_phys_id_resources.size();
			m_phys_id_resources.push_back(p_resource);
		} else
			GetResourceInfo(p_resource).dependency.phys_id = -1;

		// Assign Last Frame Pointer
		if (p_resource->GetState() == myvk_rg::interface::ResourceState::kLastFrame) {
			auto [to, _, _1] = m_resource_graph.GetOutEdges(p_resource).front();
			GetResourceInfo(p_resource).dependency.p_lf_resource = to;
		}
	}

	// Assign Root Pointers
	for (const ResourceBase *p_resource : m_phys_id_resources) {
	} */

	return {};
}

CompileResult<void> Dependency::get_pass_relation() {
	// Exclude nullptr Pass, use Local edges only
	auto view = m_pass_graph.MakeView([](const PassBase *p_pass) -> bool { return p_pass; },
	                                  kPassEdgeFilter<PassEdgeType::kLocal>);

	m_pass_relation =
	    view.TransitiveClosure([](const PassBase *p_pass) { return GetPassTopoOrder(p_pass); },
	                           [this](std::size_t topo_order) { return m_topo_order_passes[topo_order]; });
	return {};
}

} // namespace default_executor
