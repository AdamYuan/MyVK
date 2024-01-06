#include "Graph.hpp"

CompileResult<Graph> Graph::Create(const Args &args) {
	Graph g = {};

	for (const auto &it : args.render_graph.GetResultPoolData()) {
		const auto *p_result = it.second.Get<AliasBase>();
	}
}
