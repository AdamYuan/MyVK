#pragma once
#ifndef MYVK_RG_EXE_DEFAULT_GRAPH_HPP
#define MYVK_RG_EXE_DEFAULT_GRAPH_HPP

#include "Collection.hpp"

#include <unordered_map>
#include <variant>

class Graph {
public:
	struct Args {
		const RenderGraphBase &render_graph;
		const Collection &collection;
	};

	struct PassNode;
	struct ResourceNode;
	struct AccessNode {
		const InputBase *p_input;

		std::variant<const PassNode *, const ResourceNode *> source_node;
		const PassNode *dest_node;
	};
	struct PassNode {
		const PassBase *p_pass;

		std::vector<const AccessNode *> input_nodes, output_nodes;
	};
	struct ResourceNode {
		const ResourceBase *p_resource;

		const ResourceNode *opt_parent_node;
		std::vector<const ResourceNode *> child_nodes;

		const AccessNode *opt_lf_node;
		std::vector<const AccessNode *> init_nodes;
	};

private:
	std::unordered_map<const ResourceBase *, ResourceNode> m_resource_nodes;
	std::unordered_map<const PassBase *, PassNode> m_pass_nodes;
	std::unordered_map<const InputBase *, AccessNode> m_access_nodes;

	CompileResult<PassNode *> fetch_passes(const Args &args, const PassBase *p_pass);

public:
	static CompileResult<Graph> Create(const Args &args);

	inline const auto &GetResourceNodes() const { return m_resource_nodes; }
	inline const auto &GetPassNodes() const { return m_pass_nodes; }
	inline const auto &GetAccessNodes() const { return m_access_nodes; }
};

#endif // MYVK_GRAPH_HPP
