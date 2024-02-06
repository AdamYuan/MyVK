#ifndef MYVK_RG_EXE_DEFAULT_GRAPH_HPP
#define MYVK_RG_EXE_DEFAULT_GRAPH_HPP

#include "../Graph.hpp"
#include "Collection.hpp"

#include <unordered_map>
#include <variant>

namespace default_executor {

using namespace myvk_rg::interface;
using namespace myvk_rg::executor;

class Dependency {
public:
	struct Args {
		const RenderGraphBase &render_graph;
		const Collection &collection;
	};

	enum class PassEdgeType { kLocal, kLastFrame };
	struct PassEdge {
		const InputBase *opt_p_src_input, *p_dst_input;
		const ResourceBase *p_resource;
		PassEdgeType type;
	};
	enum class ResourceEdgeType { kSubResource, kLastFrame };
	struct ResourceEdge {
		ResourceEdgeType type;
	};

private:
	Graph<const PassBase *, PassEdge> m_pass_graph;
	Graph<const ResourceBase *, ResourceEdge> m_resource_graph;
	std::unordered_map<const InputBase *, const ResourceBase *> m_input_2_resource;
	std::vector<const PassBase *> m_topo_order_passes;
	std::vector<const ResourceBase *> m_id_resources;

	Relation m_pass_relation;

	CompileResult<void> traverse_pass(const Args &args, const PassBase *p_pass);
	CompileResult<void> add_war_edges(); // Write-After-Read Edges
	CompileResult<void> tag_passes();
	CompileResult<void> tag_resources();
	CompileResult<void> get_pass_relation();

public:
	static CompileResult<Dependency> Create(const Args &args);

	template <typename TypeEnum, TypeEnum Type>
	inline static const auto kEdgeFilter = [](const auto &e) { return e.type == Type; };
	template <PassEdgeType Type> inline static const auto kPassEdgeFilter = kEdgeFilter<PassEdgeType, Type>;
	template <ResourceEdgeType Type> inline static const auto kResourceEdgeFilter = kEdgeFilter<ResourceEdgeType, Type>;

	inline const auto &GetResourceGraph() const { return m_resource_graph; }
	inline const auto &GetPassGraph() const { return m_pass_graph; }

	// Map Input to Resource
	inline const ResourceBase *GetInputResource(const InputBase *p_input) const {
		return m_input_2_resource.at(p_input);
	}

	// Counts
	inline std::size_t GetPassCount() const {
		// -1 to exclude nullptr pass
		return m_pass_graph.GetVertices().size() - 1;
	}
	inline std::size_t GetResourceCount() const { return m_resource_graph.GetVertices().size(); }

	// Topological Order for Passes
	static inline std::size_t GetPassTopoOrder(const PassBase *p_pass) {
		return GetPassInfo(p_pass).dependency.topo_order - 1;
	}
	inline const PassBase *GetTopoOrderPass(std::size_t topo_order) const {
		// +1 to skip the first nullptr
		return m_topo_order_passes[topo_order + 1];
	}

	// Resource IDs

	// Relations
	inline bool IsPassPrior(const PassBase *p_pass_l, const PassBase *p_pass_r) const {
		std::size_t topo_order_l = GetPassInfo(p_pass_l).dependency.topo_order;
		std::size_t topo_order_r = GetPassInfo(p_pass_r).dependency.topo_order;
		return m_pass_relation.Get(topo_order_l, topo_order_r);
	}
	inline bool IsPassPrior(std::size_t pass_topo_order_l, std::size_t pass_topo_order_r) const {
		return m_pass_relation.Get(pass_topo_order_l + 1, pass_topo_order_r + 1);
	}
};

} // namespace default_executor

#endif // MYVK_GRAPH_HPP
