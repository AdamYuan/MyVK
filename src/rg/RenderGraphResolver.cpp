#include <myvk_rg/_details_/Input.hpp>
#include <myvk_rg/_details_/Pass.hpp>
#include <myvk_rg/_details_/RenderGraphResolver.hpp>
#include <myvk_rg/_details_/Resource.hpp>

#include <list>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace myvk_rg::_details_ {

struct RenderGraphResolver::Graph {
	struct Edge {
		const PassBase *from{}, *to{};
		const ResourceBase *resource{};
		const Input *p_input{};
	};
	struct Node {
		std::vector<const Edge *> input_edges, output_edges;
		uint32_t in_degree{};
	};
	std::unordered_map<const ImageBase *, bool> internal_image_set;
	std::unordered_set<const ManagedBuffer *> internal_buffer_set;
	std::unordered_map<const PassBase *, Node> nodes;
	std::list<Edge> edges;

	inline void add_edge(const PassBase *from, const PassBase *to, const ResourceBase *resource, const Input *p_input) {
		edges.push_back({from, to, resource, p_input});
		const auto *edge = &edges.back();
		nodes[from].output_edges.push_back(edge);
		nodes[to].input_edges.push_back(edge);
		++nodes[to].in_degree;
	}
	inline void add_internal_buffer(const ManagedBuffer *buffer) { internal_buffer_set.insert(buffer); }
	inline void add_internal_image(const ImageBase *image, bool set_has_parent = false) {
		auto it = internal_image_set.find(image);
		if (it == internal_image_set.end())
			internal_image_set[image] = set_has_parent;
		else if (set_has_parent)
			it->second = true;
	}
};

struct RenderGraphResolver::OrderedPassGraph {
	struct Edge {
		uint32_t pass_from{}, pass_to{};
		const ResourceBase *resource{};
		const Input *p_input{};
	};
	struct Node {
		const PassBase *pass;
		std::vector<const Edge *> input_edges, output_edges;
	};
	std::vector<Node> nodes;
	std::list<Edge> edges;

	inline void add_edge(uint32_t pass_from, uint32_t pass_to, const ResourceBase *resource, const Input *p_input) {
		edges.push_back({pass_from, pass_to, resource, p_input});
		const auto *edge = &edges.back();
		if (~pass_from)
			nodes[pass_from].output_edges.push_back(edge);
		nodes[pass_to].input_edges.push_back(edge);
	}
	/* inline bool is_visited(uint32_t pass) const { return nodes[pass].visited; }
	inline void set_visited(uint32_t pass) const { nodes[pass].visited = true; }
	inline void clear_visited() const {
	    for (auto &node : nodes)
	        node.visited = false;
	} */
};

void RenderGraphResolver::_visit_resource_dep_passes(RenderGraphResolver::Graph *p_graph, const ResourceBase *resource,
                                                     const PassBase *pass, const Input *p_input) {
	const auto add_edge = [p_graph, pass, p_input](const ResourceBase *resource, const PassBase *dep_pass) -> void {
		if (pass)
			p_graph->add_edge(dep_pass, pass, resource, p_input);
	};
	const auto add_edge_and_visit_dep_pass = [p_graph, &add_edge](const ResourceBase *resource,
	                                                              const PassBase *dep_pass) -> void {
		bool not_visited = p_graph->nodes.find(dep_pass) == p_graph->nodes.end();
		add_edge(resource, dep_pass);
		if (dep_pass && not_visited) {
			// Further Traverse dep_pass's dependent Resources
			dep_pass->for_each_input([p_graph, dep_pass](const Input *p_input) {
				_visit_resource_dep_passes(p_graph, p_input->GetResource(), dep_pass, p_input);
			});
		}
	};
	resource->Visit([p_graph, &add_edge_and_visit_dep_pass, &add_edge](auto *resource) -> void {
		constexpr auto kClass = ResourceVisitorTrait<decltype(resource)>::kClass;
		// For CombinedImage, further For Each its Child Images
		if constexpr (kClass == ResourceClass::kCombinedImage) {
			p_graph->add_internal_image(resource);
			// Visit Each SubImage
			resource->ForEachImage([p_graph, &add_edge_and_visit_dep_pass, &add_edge](auto *sub_image) -> void {
				if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kClass == ResourceClass::kManagedImage) {
					p_graph->add_internal_image(sub_image, true);
					add_edge(sub_image, nullptr);
				} else if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kIsAlias) {
					p_graph->add_internal_image(sub_image->GetPointedResource(), true);
					add_edge_and_visit_dep_pass(sub_image->GetPointedResource(), sub_image->GetProducerPass());
				} else
					assert(false);
			});
		} else {
			if constexpr (kClass == ResourceClass::kManagedImage) {
				p_graph->add_internal_image(resource);
				add_edge(resource, nullptr);
			} else if constexpr (kClass == ResourceClass::kManagedBuffer) {
				p_graph->add_internal_buffer(resource);
				add_edge(resource, nullptr);
			} else if constexpr (GetResourceState(kClass) == ResourceState::kAlias)
				add_edge_and_visit_dep_pass(resource->GetPointedResource(), resource->GetProducerPass());
		}
	});
}

void RenderGraphResolver::_insert_write_after_read_edges(RenderGraphResolver::Graph *p_graph) {
	std::unordered_map<const ResourceBase *, const PassBase *> write_outputs;
	for (auto &pair : p_graph->nodes) {
		auto &node = pair.second;
		for (const auto *p_edge : node.output_edges) {
			if (p_edge->p_input && !UsageIsReadOnly(p_edge->p_input->GetUsage())) {
				assert(write_outputs.find(p_edge->resource) ==
				       write_outputs.end()); // An output can only be written once
				write_outputs[p_edge->resource] = p_edge->to;
			}
		}
		for (const auto *p_edge : node.output_edges) {
			if (p_edge->p_input && UsageIsReadOnly(p_edge->p_input->GetUsage())) {
				auto it = write_outputs.find(p_edge->resource);
				if (it != write_outputs.end())
					p_graph->add_edge(p_edge->to, it->second, p_edge->resource, nullptr);
			}
		}
		write_outputs.clear();
	}
}

RenderGraphResolver::Graph RenderGraphResolver::make_graph(const RenderGraphBase *p_render_graph) {
	Graph graph = {};
	for (auto it = p_render_graph->m_p_result_pool_data->pool.begin();
	     it != p_render_graph->m_p_result_pool_data->pool.end(); ++it)
		_visit_resource_dep_passes(&graph, *p_render_graph->m_p_result_pool_data->ValueGet<0, ResourceBase *>(it));
	_insert_write_after_read_edges(&graph);
	return graph;
}

RenderGraphResolver::OrderedPassGraph RenderGraphResolver::make_ordered_pass_graph(Graph &&graph) {
	OrderedPassGraph ordered_pass_graph;

	std::queue<const PassBase *> candidate_queue;

	assert(graph.nodes.find(nullptr) != graph.nodes.end());
	assert(graph.nodes[nullptr].in_degree == 0);
	for (auto *p_edge : graph.nodes[nullptr].output_edges) {
		uint32_t degree = --graph.nodes[p_edge->to].in_degree;
		if (degree == 0)
			candidate_queue.push(p_edge->to);
	}
	while (!candidate_queue.empty()) {
		const PassBase *pass = candidate_queue.front();
		candidate_queue.pop();

		pass->m_internal_info.ordered_pass_id = ordered_pass_graph.nodes.size();
		ordered_pass_graph.nodes.push_back({pass});

		for (auto *p_edge : graph.nodes[pass].output_edges) {
			uint32_t degree = --graph.nodes[p_edge->to].in_degree;
			if (degree == 0)
				candidate_queue.push(p_edge->to);
		}
	}
	for (const auto &edge : graph.edges)
		ordered_pass_graph.add_edge(edge.from ? edge.from->m_internal_info.ordered_pass_id : -1,
		                            edge.to->m_internal_info.ordered_pass_id, edge.resource, edge.p_input);

	return ordered_pass_graph;
}

void RenderGraphResolver::extract_resources(const Graph &graph) {
	{
		m_internal_buffers.clear();
		m_internal_buffers.reserve(graph.internal_buffer_set.size());
		for (auto *buffer : graph.internal_buffer_set) {
			buffer->m_internal_info.buffer_id = m_internal_buffers.size();
			m_internal_buffers.emplace_back();
			m_internal_buffers.back().buffer = buffer;
		}
	}
	{
		m_internal_images.clear();
		m_internal_image_views.clear();
		m_internal_image_views.reserve(graph.internal_image_set.size());
		for (auto &it : graph.internal_image_set) {
			it.first->Visit([this, has_parent = it.second](auto *image) -> void {
				if constexpr (ResourceVisitorTrait<decltype(image)>::kIsInternal) {
					image->m_internal_info.image_view_id = m_internal_image_views.size();
					m_internal_image_views.emplace_back();
					m_internal_image_views.back().image = image;
					m_internal_image_views.back().has_parent = has_parent;
				}
			});
		}
		for (auto &it : graph.internal_image_set) {
			if (!it.second)
				it.first->Visit([this](auto *image) -> void {
					if constexpr (ResourceVisitorTrait<decltype(image)>::kIsInternal) {
						image->m_internal_info.image_id = m_internal_images.size();
						m_internal_images.emplace_back();
						m_internal_images.back().image = image;
						if constexpr (ResourceVisitorTrait<decltype(image)>::kClass == ResourceClass::kCombinedImage)
							_initialize_combined_image(image);
					}
				});
		}
	}
	// Compute Image View Relation
	m_image_view_parent_relation.Reset(GetInternalImageViewCount(), GetInternalImageViewCount());
	for (uint32_t image_view_id = 0; image_view_id < GetInternalImageViewCount(); ++image_view_id) {
		m_internal_image_views[image_view_id].image->Visit([this, image_view_id](const auto *image) -> void {
			if constexpr (ResourceVisitorTrait<decltype(image)>::kClass == ResourceClass::kCombinedImage)
				image->ForAllImages([this, image_view_id](const auto *sub_image) -> void {
					if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kIsInternal)
						m_image_view_parent_relation.SetRelation(image_view_id, GetInternalImageViewID(sub_image));
				});
		});
	}
}

void RenderGraphResolver::_initialize_combined_image(const CombinedImage *image) {
	// Visit Each Child Image, Update Size and Base Layer
	image->ForEachExpandedImage([image, parent = image->m_internal_info.parent ? image->m_internal_info.parent
	                                                                           : image](auto *sub_image) -> void {
		if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kIsCombinedOrManagedImage) {
			sub_image->m_internal_info.image_id = image->m_internal_info.image_id;
			sub_image->m_internal_info.parent = parent;
			// Merge the Size of the Current Child Image
			if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kClass == ResourceClass::kCombinedImage)
				_initialize_combined_image(sub_image); // Further Query SubImage Size
		}
	});
}

/* RenderGraphResolver::RelationMatrix
RenderGraphResolver::_extract_transitive_closure(const OrderedPassGraph &ordered_pass_graph) {
    RelationMatrix relation;
    const uint32_t pass_count = ordered_pass_graph.nodes.size();
    relation.Reset(pass_count, pass_count);
    for (uint32_t i = pass_count - 1; ~i; --i) {
        for (const auto *p_edge : ordered_pass_graph.nodes[i].input_edges)
            if (~p_edge->pass_from) {
                relation.SetRelation(p_edge->pass_from, i);
                relation.ApplyRelations(i, p_edge->pass_from);
            }
    }
    return relation;
} */

void RenderGraphResolver::initialize_naive_resource_relation(const OrderedPassGraph &ordered_pass_graph) {
	const uint32_t pass_count = ordered_pass_graph.nodes.size();

	RelationMatrix pass_resource_not_prior_relation;
	{
		pass_resource_not_prior_relation.Reset(pass_count, GetInternalResourceCount());
		for (uint32_t pass_id = 0; pass_id < pass_count; ++pass_id) {
			for (const auto *p_edge : ordered_pass_graph.nodes[pass_id].input_edges)
				if (~p_edge->pass_from)
					pass_resource_not_prior_relation.ApplyRelations(p_edge->pass_from, pass_id);
			ordered_pass_graph.nodes[pass_id].pass->for_each_input([this, pass_id, &pass_resource_not_prior_relation](
			                                                           const Input *p_input) {
				pass_resource_not_prior_relation.SetRelation(pass_id, GetInternalResourceID(p_input->GetResource()));
			});
		}
	}

	RelationMatrix resource_not_prior_relation;
	{
		resource_not_prior_relation.Reset(GetInternalResourceCount(), GetInternalResourceCount());
		for (uint32_t pass_id = 0; pass_id < pass_count; ++pass_id) {
			ordered_pass_graph.nodes[pass_id].pass->for_each_input(
			    [this, pass_id, &pass_resource_not_prior_relation, &resource_not_prior_relation](const Input *p_input) {
				    resource_not_prior_relation.ApplyRelations(pass_resource_not_prior_relation, pass_id,
				                                               GetInternalResourceID(p_input->GetResource()));
			    });
		}
	}

	m_resource_conflicted_relation.Reset(GetInternalResourceCount(), GetInternalResourceCount());
	for (uint32_t resource_id_0 = 0; resource_id_0 < GetInternalResourceCount(); ++resource_id_0) {
		m_resource_conflicted_relation.SetRelation(resource_id_0, resource_id_0);
		for (uint32_t resource_id_1 = 0; resource_id_1 < resource_id_0; ++resource_id_1) {
			if (resource_not_prior_relation.GetRelation(resource_id_0, resource_id_1) &&
			    resource_not_prior_relation.GetRelation(resource_id_1, resource_id_0)) {
				m_resource_conflicted_relation.SetRelation(resource_id_0, resource_id_1);
				m_resource_conflicted_relation.SetRelation(resource_id_1, resource_id_0);
			}
		}
	}
}

void RenderGraphResolver::Resolve(const RenderGraphBase *p_render_graph) {
	Graph graph = make_graph(p_render_graph);
	extract_resources(graph);
	OrderedPassGraph ordered_pass_graph = make_ordered_pass_graph(std::move(graph));
	initialize_naive_resource_relation(ordered_pass_graph);
}

} // namespace myvk_rg::_details_
