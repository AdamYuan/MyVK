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
	std::vector<const ResourceBase *> m_phys_id_resources;

	Relation m_pass_relation;

	CompileResult<void> traverse_pass(const Args &args, const PassBase *p_pass);
	CompileResult<void> add_war_edges(); // Write-After-Read Edges
	CompileResult<void> sort_passes();
	CompileResult<void> tag_resources();
	CompileResult<void> get_pass_relation();

public:
	static CompileResult<Dependency> Create(const Args &args);

	template <typename TypeEnum, TypeEnum Type>
	inline static const auto kEdgeFilter = [](const auto &e) { return e.type == Type; };
	template <PassEdgeType Type> inline static const auto kPassEdgeFilter = kEdgeFilter<PassEdgeType, Type>;
	template <ResourceEdgeType Type> inline static const auto kResourceEdgeFilter = kEdgeFilter<ResourceEdgeType, Type>;
	inline static const auto kAnyFilter = [](auto &&) { return true; };

	inline const auto &GetResourceGraph() const { return m_resource_graph; }
	inline const auto &GetPassGraph() const { return m_pass_graph; }

	// Map Input to Resource
	inline const ResourceBase *GetInputResource(const InputBase *p_input) const {
		return m_input_2_resource.at(p_input);
	}

	// Counts
	inline std::size_t GetPassCount() const { return m_topo_order_passes.size(); }
	inline std::size_t GetResourceCount() const { return m_resource_graph.GetVertices().size(); }
	inline std::size_t GetPhysResourceCount() const {
		// Physical Resource Count
		return m_phys_id_resources.size();
	}

	// Topological Order for Passes
	static inline std::size_t GetPassTopoOrder(const PassBase *p_pass) {
		return GetPassInfo(p_pass).dependency.topo_order;
	}
	inline const PassBase *GetTopoOrderPass(std::size_t topo_order) const { return m_topo_order_passes[topo_order]; }

	// Resource Physical IDs
	static inline std::size_t GetResourcePhysID(const ResourceBase *p_resource) {
		return GetResourceInfo(p_resource).dependency.phys_id;
	}
	inline const ResourceBase *GetPhysIDResource(std::size_t phys_id) const { return m_phys_id_resources[phys_id]; }

	// Relations
	inline bool IsPassPrior(const PassBase *p_pass_l, const PassBase *p_pass_r) const {
		return m_pass_relation.Get(GetPassTopoOrder(p_pass_l), GetPassTopoOrder(p_pass_r));
	}
	inline bool IsPassPrior(std::size_t pass_topo_order_l, std::size_t pass_topo_order_r) const {
		return m_pass_relation.Get(pass_topo_order_l, pass_topo_order_r);
	}
};

} // namespace default_executor

#endif // MYVK_GRAPH_HPP
