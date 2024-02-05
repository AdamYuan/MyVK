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

	enum class EdgeType { kLocal, kLastFrame };

	struct PassEdge {
		const InputBase *opt_p_src_input, *p_dst_input;
		const ResourceBase *p_resource;
		EdgeType type;
	};
	struct ResourceEdge {
		EdgeType type;
	};

private:
	Graph<const PassBase *, PassEdge> m_pass_graph;
	Graph<const ResourceBase *, ResourceEdge> m_resource_graph;
	std::unordered_map<const InputBase *, const ResourceBase *> m_input_2_resource;
	std::vector<const PassBase *> m_topo_sorted_passes;

	CompileResult<void> traverse_pass(const Args &args, const PassBase *p_pass);
	CompileResult<void> add_war_edges(); // Write-After-Read Edges
	CompileResult<void> topo_sort_pass();

public:
	static CompileResult<Dependency> Create(const Args &args);

	inline static const auto kLocalEdgeFilter = [](const auto &e) { return e.type == EdgeType::kLocal; };
	inline static const auto kLastFrameEdgeFilter = [](const auto &e) { return e.type == EdgeType::kLastFrame; };

	inline const auto &GetResourceGraph() const { return m_resource_graph; }
	inline const auto &GetPassGraph() const { return m_pass_graph; }
	inline const ResourceBase *GetInputResource(const InputBase *p_input) const {
		return m_input_2_resource.at(p_input);
	}
	static inline uint32_t GetPassTopoOrder(const PassBase *p_pass) { return GetPassInfo(p_pass).dependency.topo_order; }
	inline const PassBase *GetTopoOrderPass(uint32_t topo_order) const { return m_topo_sorted_passes[topo_order]; }
};

#endif // MYVK_GRAPH_HPP
