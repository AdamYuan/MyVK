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
	std::vector<const PassBase *> m_topo_id_passes;
	std::vector<const ResourceBase *> m_phys_id_resources;

	Relation m_pass_relation, m_resource_relation;

	void traverse_pass(const Args &args, const PassBase *p_pass);
	const InputBase *traverse_output_alias(const Dependency::Args &args, const OutputAlias auto &output_alias);
	void add_war_edges(); // Write-After-Read Edges
	void sort_passes();
	void tag_resources();
	void get_pass_relation();
	void get_resource_relation();

	static auto &get_dep_info(const PassBase *p_pass) { return GetPassInfo(p_pass).dependency; }
	static auto &get_dep_info(const InputBase *p_input) { return GetInputInfo(p_input).dependency; }
	static auto &get_dep_info(const ResourceBase *p_resource) { return GetResourceInfo(p_resource).dependency; }

public:
	static Dependency Create(const Args &args);

	template <typename TypeEnum, TypeEnum... Types>
	inline static const auto kEdgeFilter = [](const auto &e) { return ((e.type == Types) || ...); };
	template <PassEdgeType... Types> inline static const auto kPassEdgeFilter = kEdgeFilter<PassEdgeType, Types...>;
	template <ResourceEdgeType... Types>
	inline static const auto kResourceEdgeFilter = kEdgeFilter<ResourceEdgeType, Types...>;
	inline static const auto kAnyFilter = [](auto &&) { return true; };

	inline const auto &GetResourceGraph() const { return m_resource_graph; }
	inline const auto &GetPassGraph() const { return m_pass_graph; }

	// Input
	inline const ResourceBase *GetInputResource(const InputBase *p_input) const {
		return get_dep_info(p_input).p_resource;
	}
	inline const PassBase *GetInputPass(const InputBase *p_input) const { return get_dep_info(p_input).p_pass; }

	// Counts
	inline std::size_t GetSortedPassCount() const { return m_topo_id_passes.size(); }
	inline std::size_t GetPhysResourceCount() const { return m_phys_id_resources.size(); }

	// Topological Ordered ID for Passes
	static std::size_t GetPassTopoID(const PassBase *p_pass) { return GetPassInfo(p_pass).dependency.topo_id; }
	const PassBase *GetTopoIDPass(std::size_t topo_order) const { return m_topo_id_passes[topo_order]; }
	const auto &GetTopoIDPasses() const { return m_topo_id_passes; }

	// Physical ID for Resources
	static std::size_t GetResourcePhysID(const ResourceBase *p_resource) { return get_dep_info(p_resource).phys_id; }
	const ResourceBase *GetPhysIDResource(std::size_t phys_id) const { return m_phys_id_resources[phys_id]; }
	const auto &GetPhysIDResources() const { return m_phys_id_resources; }

	// Resource Pointers
	static const ResourceBase *GetRootResource(const ResourceBase *p_resource) {
		return get_dep_info(p_resource).p_root_resource;
	}
	static bool IsRootResource(const ResourceBase *p_resource) {
		return get_dep_info(p_resource).p_root_resource == p_resource;
	}
	static const ResourceBase *GetLFResource(const ResourceBase *p_resource) {
		return get_dep_info(p_resource).p_lf_resource;
	}

	// Resource Accesses
	static auto GetResourceAccessPassBitset(const ResourceBase *p_resource) {
		return get_dep_info(p_resource).access_passes;
	}

	// Relations
	inline bool IsPassLess(std::size_t topo_id_l, std::size_t topo_id_r) const {
		return m_pass_relation.Get(topo_id_l, topo_id_r);
	}
	inline bool IsPassLess(const PassBase *p_l, const PassBase *p_r) const {
		return IsPassLess(GetPassTopoID(p_l), GetPassTopoID(p_r));
	}
	inline bool IsResourceLess(std::size_t phys_id_l, std::size_t phys_id_r) const {
		return m_resource_relation.Get(phys_id_l, phys_id_r);
	}
	inline bool IsResourceLess(const ResourceBase *p_l, const ResourceBase *p_r) const {
		return IsResourceLess(GetResourcePhysID(p_l), GetResourcePhysID(p_r));
	}
	inline bool IsResourceConflicted(std::size_t phys_id_0, std::size_t phys_id_1) const {
		return !IsResourceLess(phys_id_0, phys_id_1) && !IsResourceLess(phys_id_1, phys_id_0);
	}
	inline bool IsResourceConflicted(const ResourceBase *p_0, const ResourceBase *p_1) const {
		return IsResourceConflicted(GetResourcePhysID(p_0), GetResourcePhysID(p_1));
	}
};

} // namespace default_executor

#endif // MYVK_GRAPH_HPP
