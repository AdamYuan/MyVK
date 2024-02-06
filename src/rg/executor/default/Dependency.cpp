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
	UNWRAP(g.tag_passes());
	UNWRAP(g.get_pass_relation());

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
	for (const PassBase *p_pass : m_pass_graph.GetVertices()) {
		std::unordered_map<const ResourceBase *, AccessEdgeInfo> access_edges;

		for (auto [_, e, edge_id] : m_pass_graph.GetOutEdges(p_pass, kPassEdgeFilter<PassEdgeType::kLocal>)) {
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

CompileResult<void> Dependency::tag_passes() {
	auto kahn_result = m_pass_graph.KahnTopologicalSort(kPassEdgeFilter<PassEdgeType::kLocal>,
	                                                    std::initializer_list<const PassBase *>{nullptr});

	if (!kahn_result.is_dag)
		return error::PassCycleExist{};

	m_topo_order_passes = std::move(kahn_result.sorted);
	// Assign topo-order to passes, (skip first nullptr pass)
	for (std::size_t topo_order = 1u; const PassBase *p_pass : std::span{m_topo_order_passes}.subspan<1>())
		GetPassInfo(p_pass).dependency.topo_order = topo_order++;

	return {};
}

CompileResult<void> Dependency::tag_resources() {
	for (std::size_t id = 0; const ResourceBase *p_resource : m_resource_graph.GetVertices()) {
		GetResourceInfo(p_resource).dependency.id = id++;

		auto in_edges = m_resource_graph.GetInEdges(p_resource);
		std::size_t in_edge_count = 0, sub_resource_in_edge_count = 0;
		for (auto [_, e, _1] : in_edges) {
			++in_edge_count;
			sub_resource_in_edge_count += e.type == ResourceEdgeType::kSubResource;
		}

		if (sub_resource_in_edge_count > 1) {
			// Resource should not be shared by multiple CombinedImage
			return error::ResourceMultiParent{.key = p_resource->GetGlobalKey()};
		}
		if (p_resource->GetState() == myvk_rg::interface::ResourceState::kLastFrame && in_edge_count) {
			// LastFrame Resource should not have parent resource
			return error::ResourceLFParent{.key = p_resource->GetGlobalKey()};
		}
	}

	return {};
}

CompileResult<void> Dependency::get_pass_relation() {
	m_pass_relation = m_pass_graph.TransitiveClosure(
	    kPassEdgeFilter<PassEdgeType::kLocal>,
	    [](const PassBase *p_pass) { return p_pass ? GetPassInfo(p_pass).dependency.topo_order : 0; },
	    [this](std::size_t topo_order) { return m_topo_order_passes[topo_order]; });
	return {};
}

} // namespace default_executor
