#include "Graph.hpp"

CompileResult<Graph> Graph::Create(const Args &args) {
	Graph g = {};

	for (const auto &it : args.render_graph.GetResultPoolData()) {
		const InputBase *p_input;
		it.second.Visit([&](const auto *p_alias) -> CompileResult<void> {
			UNWRAP_ASSIGN(p_input, args.collection.FindAliasSource(*p_alias));
			return {};
		});
	}
}
