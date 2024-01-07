#include "Graph.hpp"

CompileResult<Graph> Graph::Create(const Args &args) {
	Graph g = {};

	for (const auto &it : args.render_graph.GetResultPoolData()) {
		UNWRAP(it.second.Visit([&](const auto *p_alias) -> CompileResult<void> {
			const PassBase *p_pass;
			UNWRAP_ASSIGN(p_pass, args.collection.FindPass(p_alias->GetSourcePassKey()));
			UNWRAP(g.fetch_passes(args, p_pass));
			return {};
		}));
	}
	return g;
}
CompileResult<Graph::PassNode *> Graph::fetch_passes(const Args &args, const PassBase *p_pass) {
	{ // If exist, return
		auto it = m_pass_nodes.find(p_pass);
		if (it != m_pass_nodes.end())
			return &it->second;
	}
	auto &pass_node = m_pass_nodes[p_pass];
	pass_node.p_pass = p_pass;

	const auto pass_visitor = overloaded(
	    [&](const PassWithInput auto *p_pass) -> CompileResult<void> {
		    for (const auto &it : p_pass->GetInputPoolData()) {
			    const InputBase *p_input = it.second.template Get<InputBase>();
			    auto &access_node = m_access_nodes[p_input];
			    access_node.p_input = p_input;
			    access_node.dest_node = &pass_node;

			    pass_node.input_nodes.push_back(&access_node);

			    UNWRAP(p_input->GetInputAlias().Visit(overloaded(
			        [&](const OutputAlias auto *p_output_alias) -> CompileResult<void> {
				        const PassBase *p_pass;
				        UNWRAP_ASSIGN(p_pass, args.collection.FindPass(p_output_alias->GetSourcePassKey()));
				        PassNode *p_prev_pass_node;
				        UNWRAP_ASSIGN(p_prev_pass_node, fetch_passes(args, p_pass));

				        access_node.source_node = p_prev_pass_node;
				        p_prev_pass_node->output_nodes.push_back(&access_node);
				        return {};
			        },
			        [&](const RawAlias auto *p_raw_alias) -> CompileResult<void> {
				        const ResourceBase *p_resource;
				        UNWRAP_ASSIGN(p_resource, args.collection.FindResource(p_raw_alias->GetSourceKey()));
				        auto &resource_node = m_resource_nodes[p_resource];
				        resource_node.p_resource = p_resource;
				        resource_node.init_nodes.push_back(&access_node);

				        access_node.source_node = &resource_node;
				        return {};
			        })));
		    }
		    return {};
	    },
	    [](const auto *) -> CompileResult<void> { return {}; });

	UNWRAP(p_pass->Visit(pass_visitor));
	return &pass_node;
}
