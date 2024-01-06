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
		const ResourceNode *p_resource_node;
	};
	struct InputEdge {
		const AccessNode *p_access_node;
		std::variant<const ResourceNode *, const PassNode *> p_source_node;
	};
	struct OutputEdge {
		const AccessNode *p_access_node;
		const PassNode *p_dest_node;
	};
	struct PassNode {
		const PassBase *p_pass;
		std::vector<InputEdge> input_edges;
		std::vector<OutputEdge> output_edges;
	};
	struct ResourceNode {
		const ResourceBase *p_resource;
		const ResourceNode *p_parent_node;
		std::vector<const ResourceNode *> p_child_nodes;
	};

private:
	std::unordered_map<const ResourceBase *, ResourceNode> m_resource_nodes;
	std::unordered_map<const PassBase *, PassNode> m_pass_nodes;
	std::unordered_map<const InputBase *, AccessNode> m_access_nodes;

	CompileResult<void> fetch_passes(const Args &args, const PassBase *pass);

public:
	static CompileResult<Graph> Create(const Args &args);

	inline const auto &GetResourceNodes() const { return m_resource_nodes; }
	inline const auto &GetPassNodes() const { return m_pass_nodes; }
	inline const auto &GetAccessNodes() const { return m_access_nodes; }
};

#endif // MYVK_GRAPH_HPP
