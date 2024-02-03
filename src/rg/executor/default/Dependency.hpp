#ifndef MYVK_RG_EXE_DEFAULT_GRAPH_HPP
#define MYVK_RG_EXE_DEFAULT_GRAPH_HPP

#include "Collection.hpp"
#include "Graph.hpp"

#include <unordered_map>
#include <variant>

class Dependency {
public:
	struct Args {
		const RenderGraphBase &render_graph;
		const Collection &collection;
	};

	struct PassNode;
	struct ResourceNode;
	struct PassEdge {
		const InputBase *opt_p_src_input, *p_dst_input;
		const ResourceBase *p_resource;
	};
	struct ResourceNode {
		const ResourceBase *p_resource;

		const ResourceNode *opt_parent_node, *opt_lf_node;
		std::vector<const ResourceNode *> child_nodes;
	};


private:
	Graph<const PassBase *, PassEdge> m_pass_graph;
	std::unordered_map<const ResourceBase *, ResourceNode> m_resource_nodes;

	CompileResult<void> fetch_passes(const Args &args, const PassBase *p_pass);
	CompileResult<void> fetch_res_relations(const Args &args, ResourceNode *p_res_node) const;

public:
	static CompileResult<Dependency> Create(const Args &args);

	inline const auto &GetResourceNodes() const { return m_resource_nodes; }
	inline const auto &GetPassGraph() const { return m_pass_graph; }
};

#endif // MYVK_GRAPH_HPP
