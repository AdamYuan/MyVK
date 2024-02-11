#include "Dependency.hpp"

namespace default_executor {

Dependency Dependency::Create(const Args &args) {
	args.collection.ClearInfo(&PassInfo::dependency, &InputInfo::dependency, &ResourceInfo::dependency);

	Dependency g = {};

	for (const auto &it : args.render_graph.GetResultPoolData())
		it.second.Visit([&](const auto *p_alias) { g.traverse_output_alias(args, *p_alias); });
	g.add_war_edges();
	g.sort_passes();
	g.get_pass_relation();

	g.tag_resources();
	g.get_resource_relation();

	return g;
}

const InputBase *Dependency::traverse_output_alias(const Dependency::Args &args, const OutputAlias auto &output_alias) {
	const PassBase *p_src_pass = args.collection.FindPass(output_alias.GetSourcePassKey());
	const InputBase *p_src_input = args.collection.FindInput(output_alias.GetSourceKey());
	traverse_pass(args, p_src_pass);
	return p_src_input;
}

void Dependency::traverse_pass(const Args &args, const PassBase *p_pass) {
	if (m_pass_graph.HasVertex(p_pass))
		return;

	const auto pass_visitor = [&](const PassWithInput auto *p_pass) {
		for (const auto &it : p_pass->GetInputPoolData()) {
			const InputBase *p_input = it.second.template Get<InputBase>();
			get_dep_info(p_input).p_pass = p_pass;
			get_dep_info(p_pass).inputs.push_back(p_input);

			p_input->GetInputAlias().Visit(overloaded(
			    [&](const OutputAlias auto *p_output_alias) {
				    const InputBase *p_src_input = traverse_output_alias(args, *p_output_alias);
				    const PassBase *p_src_pass = get_dep_info(p_src_input).p_pass;
				    const ResourceBase *p_resource = get_dep_info(p_src_input).p_resource;
				    get_dep_info(p_input).p_resource = p_resource;

				    m_pass_graph.AddEdge(p_src_pass, p_pass, PassEdge{p_src_input, p_input, p_resource});
			    },
			    [&](const RawAlias auto *p_raw_alias) {
				    const ResourceBase *p_resource = args.collection.FindResource(p_raw_alias->GetSourceKey());
				    m_resource_graph.AddVertex(p_resource);
				    get_dep_info(p_input).p_resource = p_resource;

				    p_resource->Visit(overloaded(
				        [&](const CombinedImage *p_combined_image) {
					        for (const OutputImageAlias &src_alias : p_combined_image->GetSubImages()) {
						        const InputBase *p_src_input = traverse_output_alias(args, src_alias);
						        const PassBase *p_src_pass = get_dep_info(p_src_input).p_pass;
						        const ResourceBase *p_sub_image = get_dep_info(p_src_input).p_resource;

						        m_pass_graph.AddEdge(p_src_pass, p_pass, PassEdge{p_src_input, p_input, p_sub_image});
						        m_resource_graph.AddEdge(p_resource, p_sub_image, ResourceEdge::kSubResource);
					        }
				        },
				        [&](const LastFrameResource auto *p_lf_resource) {
					        m_pass_graph.AddEdge(nullptr, p_pass, PassEdge{nullptr, p_input, p_resource});

					        const auto &src_alias = p_lf_resource->GetPointedAlias();
					        const InputBase *p_src_input = traverse_output_alias(args, src_alias);
					        const PassBase *p_src_pass = get_dep_info(p_src_input).p_pass;
					        const ResourceBase *p_src_resource = get_dep_info(p_src_input).p_resource;

					        // Last Frame Edges
					        m_resource_graph.AddEdge(p_resource, p_src_resource, ResourceEdge::kLastFrame);
				        },
				        [&](auto &&) {
					        m_pass_graph.AddEdge(nullptr, p_pass, PassEdge{nullptr, p_input, p_resource});
				        }));
			    }));
		}
	};

	p_pass->Visit(overloaded(pass_visitor, [](auto &&) {}));
}

struct AccessEdgeInfo {
	std::vector<std::size_t> reads;
	std::optional<std::size_t> opt_write;
};
void Dependency::add_war_edges() {
	auto view = m_pass_graph.MakeView(kAnyFilter, kAnyFilter);

	for (const PassBase *p_pass : view.GetVertices()) {
		std::unordered_map<const ResourceBase *, AccessEdgeInfo> access_edges;

		for (auto [_, e, edge_id] : view.GetOutEdges(p_pass)) {
			auto &info = access_edges[e.p_resource];
			if (UsageIsReadOnly(e.p_dst_input->GetUsage()))
				info.reads.push_back(edge_id);
			else {
				// Forbid multiple writes
				if (info.opt_write)
					Throw(error::MultipleWrite{.alias = e.p_dst_input->GetInputAlias()});
				// Forbid writes to last frame resource
				if (e.p_resource->GetState() == myvk_rg::interface::ResourceState::kLastFrame)
					Throw(error::WriteToLastFrame{.alias = e.p_dst_input->GetInputAlias(),
					                              .pass_key = p_pass->GetGlobalKey()});

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
				                     });
			}
		}
	}
}

void Dependency::sort_passes() {
	// Exclude nullptr Pass, use Local edges only
	auto view = m_pass_graph.MakeView([](const PassBase *p_pass) -> bool { return p_pass; }, kAnyFilter);

	auto kahn_result = view.KahnTopologicalSort();

	if (!kahn_result.is_dag)
		Throw(error::PassNotDAG{});

	// Assign topo-id to passes
	m_topo_id_passes = std::move(kahn_result.sorted);
	for (std::size_t topo_id = 0; const PassBase *p_pass : m_topo_id_passes)
		get_dep_info(p_pass).topo_id = topo_id++;
}

void Dependency::tag_resources() {
	// Validate and Tag Resources
	for (const ResourceBase *p_resource : m_resource_graph.GetVertices()) {
		if (p_resource->GetState() == ResourceState::kLastFrame) {
			// Assign LF Pointers
			auto [to, _, _1] = m_resource_graph.GetOutEdges(p_resource).front();
			get_dep_info(p_resource).p_lf_resource = to;
			get_dep_info(to).p_lf_resource = p_resource;

			// LastFrame Resource should not have parent resource
			if (!m_resource_graph.GetInEdges(p_resource).empty())
				Throw(error::ResourceLFParent{.key = p_resource->GetGlobalKey()});
		} else if (p_resource->GetState() == ResourceState::kExternal) {
			// External Resource should not have parent resource
			if (!m_resource_graph.GetInEdges(p_resource).empty())
				Throw(error::ResourceExtParent{.key = p_resource->GetGlobalKey()});
		}
	}

	// Resolve Resource Tree
	auto find_trees_result = m_resource_graph.FindTrees([](const ResourceBase *p_root, const ResourceBase *p_sub) {
		if (p_root->GetState() == myvk_rg::interface::ResourceState::kLastFrame && p_sub != p_root)
			get_dep_info(p_sub).p_root_resource = get_dep_info(p_root).p_lf_resource;
		else
			get_dep_info(p_sub).p_root_resource = p_root;
	});

	if (!find_trees_result.is_forest)
		Throw(error::ResourceNotTree{});

	// Assign physical-id to resources
	for (const ResourceBase *p_resource : m_resource_graph.GetVertices())
		if (IsRootResource(p_resource)) {
			std::size_t phys_id = m_phys_id_resources.size();
			get_dep_info(p_resource).phys_id = phys_id;
			m_phys_id_resources.push_back(p_resource);
		}
	for (const ResourceBase *p_resource : m_resource_graph.GetVertices())
		if (!IsRootResource(p_resource))
			get_dep_info(p_resource).phys_id = get_dep_info(GetRootResource(p_resource)).phys_id;
}

void Dependency::get_pass_relation() {
	// Exclude nullptr Pass, use Local edges only
	auto view = m_pass_graph.MakeView([](const PassBase *p_pass) -> bool { return p_pass; }, kAnyFilter);

	m_pass_relation =
	    view.TransitiveClosure(GetPassTopoID, [this](std::size_t topo_id) { return GetTopoIDPass(topo_id); });
}

void Dependency::get_resource_relation() {
	{ // Tag access bits of physical resources
		for (const ResourceBase *p_root : m_phys_id_resources)
			get_dep_info(p_root).access_passes.Reset(GetSortedPassCount());

		// Exclude LastFrame edges
		auto view = m_pass_graph.MakeView(kAnyFilter, kAnyFilter);
		for (auto [_, to, e, _1] : view.GetEdges())
			get_dep_info(GetRootResource(e.p_resource)).access_passes.Add(GetPassTopoID(to));
	}

	// Pass < Resource
	Relation pass_resource_relation{GetSortedPassCount(), GetPhysResourceCount()};
	for (std::size_t res_phys_id = 0; const ResourceBase *p_root : m_phys_id_resources) {
		for (std::size_t pass_topo_id = 0; pass_topo_id < GetSortedPassCount(); ++pass_topo_id) {
			// Pass < Resource <==> forall x in Resource Access Passes, Pass < x
			if (m_pass_relation.All(pass_topo_id, get_dep_info(p_root).access_passes))
				pass_resource_relation.Add(pass_topo_id, res_phys_id);
		}
		++res_phys_id;
	}

	// Resource > Pass
	Relation resource_pass_relation = pass_resource_relation.GetInversed();

	// Resource < Resource
	m_resource_relation.Reset(GetPhysResourceCount(), GetPhysResourceCount());
	for (std::size_t l_phys_id = 0; const ResourceBase *p_root : m_phys_id_resources) {
		for (std::size_t r_phys_id = 0; r_phys_id < GetPhysResourceCount(); ++r_phys_id) {
			// Resource_L < Resource_R <==> forall x in Resource_L Access Passes, Resource_R > x
			if (resource_pass_relation.All(r_phys_id, get_dep_info(p_root).access_passes))
				m_resource_relation.Add(l_phys_id, r_phys_id);
		}
		++l_phys_id;
	}
}

} // namespace default_executor
