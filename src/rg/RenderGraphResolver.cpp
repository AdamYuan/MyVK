#include "RenderGraphResolver.hpp"

#include <list>
#include <queue>
#include <unordered_map>
#include <unordered_set>

#include <iostream>

namespace myvk_rg::_details_ {

struct SingleDependency {
	const ResourceBase *resource{};
	const Input *p_input_from{}, *p_input_to{};
};
struct RenderGraphResolver::Graph {
	struct Edge : public SingleDependency {
		const PassBase *pass_from{}, *pass_to{};
		bool is_read_to_write{};
		mutable bool deleted{};
	};
	struct Node {
		std::vector<const Edge *> input_edges, output_edges;
		uint32_t in_degree{};
	};
	std::unordered_map<const ImageBase *, bool> internal_image_set;
	std::unordered_set<const ManagedBuffer *> internal_buffer_set;
	std::unordered_map<const PassBase *, Node> nodes;
	std::list<Edge> edges;

	inline void add_edge(const PassBase *pass_from, const PassBase *pass_to, const ResourceBase *resource,
	                     const Input *p_input_from, const Input *p_input_to, bool is_read_to_write_edge) {
		edges.push_back({resource, p_input_from, p_input_to, pass_from, pass_to, is_read_to_write_edge});
		const auto *edge = &edges.back();
		nodes[pass_from].output_edges.push_back(edge);
		if (pass_to) {
			nodes[pass_to].input_edges.push_back(edge);
			++nodes[pass_to].in_degree;
		}
	}
	inline void add_internal_buffer(const ManagedBuffer *buffer) { internal_buffer_set.insert(buffer); }
	inline void add_internal_image(const ImageBase *image, bool set_has_parent = false) {
		auto it = internal_image_set.find(image);
		if (it == internal_image_set.end())
			internal_image_set[image] = set_has_parent;
		else if (set_has_parent)
			it->second = true;
	}
	inline void visit_resource_dep_passes(const ResourceBase *resource, const PassBase *pass, const Input *p_input) {
		const auto add_visitor_edge = [this, pass, p_input](const ResourceBase *resource, const PassBase *dep_pass,
		                                                    const Input *p_dep_input) -> void {
			add_edge(dep_pass, pass, resource, p_dep_input, p_input, false);
		};
		const auto add_edge_and_visit_dep_pass = [this, &add_visitor_edge](const ResourceBase *resource,
		                                                                   const PassBase *dep_pass,
		                                                                   const Input *p_dep_input) -> void {
			bool not_visited = nodes.find(dep_pass) == nodes.end();
			add_visitor_edge(resource, dep_pass, p_dep_input);
			if (dep_pass && not_visited) {
				// Further Traverse dep_pass's dependent Resources
				dep_pass->for_each_input([this, dep_pass](const Input *p_input) {
					visit_resource_dep_passes(p_input->GetResource(), dep_pass, p_input);
				});
			}
		};
		resource->Visit([this, &add_edge_and_visit_dep_pass, &add_visitor_edge](auto *resource) -> void {
			constexpr auto kClass = ResourceVisitorTrait<decltype(resource)>::kClass;
			// For CombinedImage, further For Each its Child Images
			if constexpr (kClass == ResourceClass::kCombinedImage) {
				add_internal_image(resource);
				// Visit Each SubImage
				resource->ForEachImage([this, &add_edge_and_visit_dep_pass,
				                        &add_visitor_edge](auto *sub_image) -> void {
					if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kClass == ResourceClass::kManagedImage) {
						add_internal_image(sub_image, true);
						add_visitor_edge(sub_image, nullptr, nullptr);
					} else if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kIsAlias) {
						add_internal_image(sub_image->GetPointedResource(), true);
						add_edge_and_visit_dep_pass(sub_image->GetPointedResource(), sub_image->GetProducerPass(),
						                            sub_image->GetProducerInput());
					} else
						assert(false);
				});
			} else {
				if constexpr (kClass == ResourceClass::kManagedImage) {
					add_internal_image(resource);
					add_visitor_edge(resource, nullptr, nullptr);
				} else if constexpr (kClass == ResourceClass::kManagedBuffer) {
					add_internal_buffer(resource);
					add_visitor_edge(resource, nullptr, nullptr);
				} else if constexpr (GetResourceState(kClass) == ResourceState::kAlias)
					add_edge_and_visit_dep_pass(resource->GetPointedResource(), resource->GetProducerPass(),
					                            resource->GetProducerInput());
			}
		});
	}
	inline void insert_write_after_read_edges() {
		std::unordered_map<const ResourceBase *, const Graph::Edge *> write_outputs;
		for (auto &pair : nodes) {
			auto &node = pair.second;
			for (auto *p_edge : node.output_edges) {
				if (!p_edge->is_read_to_write && p_edge->p_input_to &&
				    !UsageIsReadOnly(p_edge->p_input_to->GetUsage())) {

					assert(p_edge->pass_to);
					assert(write_outputs.find(p_edge->resource) ==
					       write_outputs.end()); // An output can only be written once

					write_outputs[p_edge->resource] = p_edge;
				}
			}
			for (const auto *p_edge : node.output_edges) {
				if (!p_edge->is_read_to_write && p_edge->p_input_to &&
				    UsageIsReadOnly(p_edge->p_input_to->GetUsage())) {

					assert(p_edge->pass_to);
					auto it = write_outputs.find(p_edge->resource);
					if (it != write_outputs.end()) {
						add_edge(p_edge->pass_to, it->second->pass_to, p_edge->resource, p_edge->p_input_to,
						         it->second->p_input_to, true);
						it->second->deleted = p_edge->resource->GetType() ==
						                      ResourceType::kBuffer; // Delete the direct write edge if a
						                                             // Write-After-Read edge exists (Buffer)
					}
				}
			}
			write_outputs.clear();
		}
	}
};

RenderGraphResolver::Graph RenderGraphResolver::make_graph(const RenderGraphBase *p_render_graph) {
	Graph graph = {};
	for (auto it = p_render_graph->m_p_result_pool_data->pool.begin();
	     it != p_render_graph->m_p_result_pool_data->pool.end(); ++it)
		graph.visit_resource_dep_passes(*p_render_graph->m_p_result_pool_data->ValueGet<0, ResourceBase *>(it), nullptr,
		                                nullptr); // TODO: Add Future Result Input
	graph.insert_write_after_read_edges();
	return graph;
}

void RenderGraphResolver::_initialize_combined_image(const CombinedImage *image) {
	// Visit Each Child Image, Update Size and Base Layer
	image->ForEachExpandedImage([image](auto *sub_image) -> void {
		if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kIsCombinedOrManagedImage) {
			sub_image->m_internal_info.image_id = image->m_internal_info.image_id;
			// sub_image->m_internal_info.parent = parent;
			// Merge the Size of the Current Child Image
			if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kClass == ResourceClass::kCombinedImage)
				_initialize_combined_image(sub_image); // Further Query SubImage Size
		}
	});
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
			it.first->Visit([this](auto *image) -> void {
				if constexpr (ResourceVisitorTrait<decltype(image)>::kIsInternal) {
					image->m_internal_info.image_view_id = m_internal_image_views.size();
					m_internal_image_views.emplace_back();
					m_internal_image_views.back().image = image;
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
	// TODO: Optimize this with ApplyRelations, or REMOVE it
	/* m_image_view_parent_relation.Reset(GetIntImageViewCount(), GetIntImageViewCount());
	for (uint32_t image_view_id = 0; image_view_id < GetIntImageViewCount(); ++image_view_id) {
	    m_internal_image_views[image_view_id].image->Visit([this, image_view_id](const auto *image) -> void {
	        if constexpr (ResourceVisitorTrait<decltype(image)>::kClass == ResourceClass::kCombinedImage)
	            image->ForAllImages([this, image_view_id](const auto *sub_image) -> void {
	                if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kIsInternal)
	                    m_image_view_parent_relation.SetRelation(image_view_id, GetIntImageViewID(sub_image));
	            });
	    });
	} */
}

struct RenderGraphResolver::OrderedPassGraph {
	struct Edge : public SingleDependency {
		uint32_t pass_from{}, pass_to{};
		bool is_image_read_to_write{}, is_extra{};
	};
	struct Node {
		const PassBase *pass;
		std::vector<const Edge *> input_edges, output_edges;
	};
	std::vector<Node> nodes;
	std::vector<Edge> edges;
	std::vector<const Edge *> src_output_edges, dst_input_edges;

	inline void add_edge(uint32_t pass_from, uint32_t pass_to, const ResourceBase *resource, const Input *p_input_from,
	                     const Input *p_input_to, bool is_image_read_to_write, bool is_extra) {
		edges.push_back({resource, p_input_from, p_input_to, pass_from, pass_to, is_image_read_to_write, is_extra});
		const auto *p_edge = &edges.back();
		(~pass_from ? nodes[pass_from].output_edges : src_output_edges).push_back(p_edge);
		(~pass_to ? nodes[pass_to].input_edges : dst_input_edges).push_back(p_edge);
	}
	inline void insert_extra_image_sequence_edges() {
		const auto process_output = [this](std::vector<const Edge *> *p_output_edges) {
			std::sort(p_output_edges->begin(), p_output_edges->end(),
			          [](const Edge *l, const Edge *r) { return l->pass_to == -1 || l->pass_to < r->pass_to; });
			std::unordered_map<const ResourceBase *, const Edge *> res_prev_edge;
			for (auto *p_edge : *p_output_edges) {
				if (p_edge->is_extra || p_edge->resource->GetType() != ResourceType::kImage)
					continue;
				auto it = res_prev_edge.find(p_edge->resource);
				if (it != res_prev_edge.end()) {
					add_edge(it->second->pass_to, p_edge->pass_to, p_edge->resource, it->second->p_input_to,
					         p_edge->p_input_to, false, true);
					it->second = p_edge;
				} else
					res_prev_edge[p_edge->resource] = p_edge;
			}
		};
		process_output(&src_output_edges);
		for (auto &node : nodes)
			process_output(&node.output_edges);
	};

	inline std::vector<uint32_t> compute_ordered_pass_merge_length() const {
		const uint32_t kOrderedPassCount = nodes.size();
		if (kOrderedPassCount == 0)
			assert(false);
		std::vector<uint32_t> merge_length(kOrderedPassCount);

		// Calculate merge_length, Complexity: O(N + M)
		// merge_length == 0: The pass is not a graphics pass
		// merge_length == 1: The pass is a graphics pass, but can't be merged
		// merge_length >  1: The pass is a graphics pass, and it can be merged to a group of _merge_length_ with the
		// passes before
		{
			merge_length[0] = nodes[0].pass->m_p_attachment_data ? 1u : 0u;
			for (uint32_t i = 1; i < kOrderedPassCount; ++i)
				merge_length[i] = nodes[i].pass->m_p_attachment_data ? merge_length[i - 1] + 1 : 0;
		}
		for (uint32_t pass_order = 0; pass_order < kOrderedPassCount; ++pass_order) {
			auto &length = merge_length[pass_order];
			if (length <= 1)
				continue;
			for (auto *p_edge : nodes[pass_order].input_edges) {
				if (!UsageIsAttachment(p_edge->p_input_to->GetUsage()) ||
				    (p_edge->p_input_from && !UsageIsAttachment(p_edge->p_input_from->GetUsage()))) {
					// If an input dependency is not attachment, then all its producers can't be merged
					// Or an input dependency is attachment, but it is not produced as an attachment, then the producer
					// can't be merged
					length = std::min(length, pass_order - p_edge->pass_from);
				} else if (p_edge->p_input_from) {
					// If the input dependencies are both attachments
					assert(~p_edge->pass_from);
					length = std::min(length, pass_order - p_edge->pass_from + merge_length[p_edge->pass_from]);
				}
			}
		}
		return merge_length;
	}
};

RenderGraphResolver::OrderedPassGraph RenderGraphResolver::make_ordered_pass_graph(Graph &&graph) {
	OrderedPassGraph ordered_pass_graph;

	std::queue<const PassBase *> candidate_queue;

	assert(graph.nodes.find(nullptr) != graph.nodes.end());
	assert(graph.nodes[nullptr].in_degree == 0);
	for (auto *p_edge : graph.nodes[nullptr].output_edges) {
		uint32_t degree = --graph.nodes[p_edge->pass_to].in_degree;
		if (degree == 0)
			candidate_queue.push(p_edge->pass_to);
	}
	while (!candidate_queue.empty()) {
		const PassBase *pass = candidate_queue.front();
		candidate_queue.pop();

		if (!pass)
			continue;

		pass->m_internal_info.pass_order = ordered_pass_graph.nodes.size();
		ordered_pass_graph.nodes.push_back({pass});

		for (auto *p_edge : graph.nodes[pass].output_edges) {
			uint32_t degree = --graph.nodes[p_edge->pass_to].in_degree;
			if (degree == 0)
				candidate_queue.push(p_edge->pass_to);
		}
	}

	ordered_pass_graph.edges.reserve(graph.edges.size() << 1); // 2-times reserved for extra image sequence edges
	for (const auto &edge : graph.edges) {
		if (edge.deleted)
			continue;
		ordered_pass_graph.add_edge(edge.pass_from ? GetPassOrder(edge.pass_from) : -1,
		                            edge.pass_to ? GetPassOrder(edge.pass_to) : -1, edge.resource, edge.p_input_from,
		                            edge.p_input_to,
		                            edge.is_read_to_write && edge.resource->GetType() == ResourceType::kImage, false);
	}

	ordered_pass_graph.insert_extra_image_sequence_edges();

	return ordered_pass_graph;
}

void RenderGraphResolver::extract_pass_relation(const OrderedPassGraph &ordered_pass_graph) {
	const uint32_t kOrderedPassCount = ordered_pass_graph.nodes.size();
	m_pass_prior_relation.Reset(kOrderedPassCount, kOrderedPassCount);
	for (uint32_t i = kOrderedPassCount - 1; ~i; --i) {
		for (const auto *p_edge : ordered_pass_graph.nodes[i].input_edges) {
			if (p_edge->is_extra)
				continue;
			if (~p_edge->pass_from) {
				m_pass_prior_relation.SetRelation(p_edge->pass_from, i);
				m_pass_prior_relation.ApplyRelations(i, p_edge->pass_from);
			}
		}
	}

	// The transpose of m_pass_prior_relation
	m_pass_after_relation.Reset(kOrderedPassCount, kOrderedPassCount);
	for (uint32_t i = 0; i < kOrderedPassCount; ++i) {
		for (const auto *p_edge : ordered_pass_graph.nodes[i].output_edges) {
			if (p_edge->is_extra)
				continue;
			if (~p_edge->pass_to) {
				m_pass_after_relation.SetRelation(p_edge->pass_to, i);
				m_pass_after_relation.ApplyRelations(i, p_edge->pass_to);
			}
		}
	}

	printf("\nPass Prior: \n");
	for (uint32_t i = 0; i < kOrderedPassCount; ++i) {
		for (uint32_t j = 0; j < kOrderedPassCount; ++j)
			printf("%d ", m_pass_prior_relation.GetRelation(i, j));
		printf("\n");
	}
	printf("\n");

	printf("\nPass After: \n");
	for (uint32_t i = 0; i < kOrderedPassCount; ++i) {
		for (uint32_t j = 0; j < kOrderedPassCount; ++j)
			printf("%d ", m_pass_after_relation.GetRelation(i, j));
		printf("\n");
	}
	printf("\n");
}

void RenderGraphResolver::extract_resource_relation(const OrderedPassGraph &ordered_pass_graph) {
	const uint32_t kOrderedPassCount = ordered_pass_graph.nodes.size();

	RelationMatrix pass_resource_not_prior_relation;
	{
		pass_resource_not_prior_relation.Reset(kOrderedPassCount, GetIntResourceCount());
		for (uint32_t pass_order = 0; pass_order < kOrderedPassCount; ++pass_order) {
			for (const auto *p_edge : ordered_pass_graph.nodes[pass_order].input_edges) {
				if (p_edge->is_extra)
					continue;
				if (~p_edge->pass_from)
					pass_resource_not_prior_relation.ApplyRelations(p_edge->pass_from, pass_order);
			}
			ordered_pass_graph.nodes[pass_order].pass->for_each_input(
			    [this, pass_order, &pass_resource_not_prior_relation](const Input *p_input) {
				    uint32_t internal_resource_id = GetIntResourceID(p_input->GetResource());
				    if (~internal_resource_id)
					    pass_resource_not_prior_relation.SetRelation(pass_order, internal_resource_id);
			    });
		}
	}

	m_resource_not_prior_relation.Reset(GetIntResourceCount(), GetIntResourceCount());
	{
		for (uint32_t pass_order = 0; pass_order < kOrderedPassCount; ++pass_order) {
			ordered_pass_graph.nodes[pass_order].pass->for_each_input(
			    [this, pass_order, &pass_resource_not_prior_relation](const Input *p_input) {
				    uint32_t internal_resource_id = GetIntResourceID(p_input->GetResource());
				    if (~internal_resource_id)
					    m_resource_not_prior_relation.ApplyRelations(pass_resource_not_prior_relation, pass_order,
					                                                 internal_resource_id);
			    });
		}
	}

	m_resource_conflict_relation.Reset(GetIntResourceCount(), GetIntResourceCount());
	for (uint32_t resource_id_0 = 0; resource_id_0 < GetIntResourceCount(); ++resource_id_0) {
		m_resource_conflict_relation.SetRelation(resource_id_0, resource_id_0);
		for (uint32_t resource_id_1 = 0; resource_id_1 < resource_id_0; ++resource_id_1) {
			if (m_resource_not_prior_relation.GetRelation(resource_id_0, resource_id_1) &&
			    m_resource_not_prior_relation.GetRelation(resource_id_1, resource_id_0)) {
				m_resource_conflict_relation.SetRelation(resource_id_0, resource_id_1);
				m_resource_conflict_relation.SetRelation(resource_id_1, resource_id_0);
			}
		}
	}

	for (uint32_t i = 0; i < GetIntResourceCount(); ++i) {
		for (uint32_t j = 0; j < GetIntResourceCount(); ++j)
			printf("%d ", m_resource_conflict_relation.GetRelation(i, j));
		printf("\n");
	}
}

void RenderGraphResolver::extract_resource_info(const RenderGraphResolver::OrderedPassGraph &ordered_pass_graph) {
	// Extract dependency_persistence, reference, last_references
	RelationMatrix resource_pass_visited;
	resource_pass_visited.Reset(GetIntResourceCount(), ordered_pass_graph.nodes.size());

	for (uint32_t order = ordered_pass_graph.nodes.size() - 1; ~order; --order) {
		const auto &node = ordered_pass_graph.nodes[order];

		const auto update_resource_reference =
		    [this, order, &node, &resource_pass_visited](const auto *resource, const Input *p_input) -> void {
			if constexpr (ResourceVisitorTrait<decltype(resource)>::kIsInternal) {
				uint32_t int_resource_id = GetIntResourceID(resource);

				if constexpr (ResourceVisitorTrait<decltype(resource)>::kType == ResourceType::kImage) {
					auto &image_info = m_internal_images[GetIntImageID(resource)];
					image_info.references.push_back({p_input, node.pass /* , resource */});
					if (!resource_pass_visited.GetRelation(int_resource_id, order))
						image_info.last_references.push_back({p_input, node.pass /* , resource */});
				} else {
					auto &buffer_info = m_internal_buffers[GetIntBufferID(resource)];
					buffer_info.references.push_back({p_input, node.pass});
					if (!resource_pass_visited.GetRelation(int_resource_id, order))
						buffer_info.last_references.push_back({p_input, node.pass});
				}

				// Exclude all the passes prior than last_reference
				assert(resource_pass_visited.GetRowSize() == m_pass_after_relation.GetRowSize());
				for (uint32_t i = 0; i < resource_pass_visited.GetRowSize(); ++i)
					resource_pass_visited.GetRowData(int_resource_id)[i] |= m_pass_after_relation.GetRowData(order)[i];
			}
		};

		const auto resource_set_persistent = [this](const auto *resource) -> void {
			if constexpr (ResourceVisitorTrait<decltype(resource)>::kIsInternal) {
				if constexpr (ResourceVisitorTrait<decltype(resource)>::kType == ResourceType::kImage) {
					auto &image_info = m_internal_images[GetIntImageID(resource)];
					image_info.dependency_persistence = true;
				} else {
					auto &buffer_info = m_internal_buffers[GetIntBufferID(resource)];
					buffer_info.dependency_persistence = true;
				}
			}
		};

		node.pass->for_each_input([&update_resource_reference, &resource_set_persistent](const Input *p_input) -> void {
			const auto local_update_resource_reference = [&update_resource_reference, p_input](const auto *resource) {
				return update_resource_reference(resource, p_input);
			};
			p_input->GetResource()->Visit(
			    [&local_update_resource_reference, &resource_set_persistent](const auto *resource) {
				    if constexpr (ResourceVisitorTrait<decltype(resource)>::kIsAlias) {
					    if (resource->GetProducerPass() == nullptr)
						    resource->GetPointedResource()->Visit(resource_set_persistent);
					    resource->GetPointedResource()->Visit(local_update_resource_reference);
				    } else
					    local_update_resource_reference(resource);
			    });
		});
	}

	// Reverse the references so that the first reference is in the earliest pass
	for (auto &image_info : m_internal_images) {
		assert(!image_info.references.empty() && !image_info.last_references.empty());
		std::reverse(image_info.references.begin(), image_info.references.end());
		printf("%ld/%ld\n", image_info.last_references.size(), image_info.references.size());
	}
	for (auto &buffer_info : m_internal_buffers) {
		assert(!buffer_info.references.empty() && !buffer_info.last_references.empty());
		std::reverse(buffer_info.references.begin(), buffer_info.references.end());
	}

	printf("Correct resource_is_prior:\n");
	for (uint32_t resource_id_0 = 0; resource_id_0 < GetIntResourceCount(); ++resource_id_0) {
		for (uint32_t resource_id_1 = 0; resource_id_1 < GetIntResourceCount(); ++resource_id_1) {
			bool is_prior = true;
			// if (IsIntResourcePrior(resource_id_0, resource_id_1))
			for (auto &ref_0 : GetIntResourceInfo(resource_id_0).references)
				for (auto &ref_1 : GetIntResourceInfo(resource_id_1).references) {
					if (!IsPassPrior(ref_0.pass, ref_1.pass))
						is_prior = false;
					// assert(IsPassPrior(ref_0.pass, ref_1.pass));
				}
			printf("%d ", is_prior);
		}
		printf("\n");
	}

	printf("Correct resource_is_prior (with last_references):\n");
	for (uint32_t resource_id_0 = 0; resource_id_0 < GetIntResourceCount(); ++resource_id_0) {
		for (uint32_t resource_id_1 = 0; resource_id_1 < GetIntResourceCount(); ++resource_id_1) {
			bool is_prior = true;
			// if (IsIntResourcePrior(resource_id_0, resource_id_1))
			for (auto &ref_0 : GetIntResourceInfo(resource_id_0).last_references)
				for (auto &ref_1 : GetIntResourceInfo(resource_id_1).references) {
					if (!IsPassPrior(ref_0.pass, ref_1.pass))
						is_prior = false;
					// assert(IsPassPrior(ref_0.pass, ref_1.pass));
				}
			printf("%d ", is_prior);
		}
		printf("\n");
	}

	printf("Origin resource_is_prior:\n");
	for (uint32_t resource_id_0 = 0; resource_id_0 < GetIntResourceCount(); ++resource_id_0) {
		for (uint32_t resource_id_1 = 0; resource_id_1 < GetIntResourceCount(); ++resource_id_1) {
			printf("%d ", IsIntResourcePrior(resource_id_0, resource_id_1));
		}
		printf("\n");
	}

	printf("Origin resource_not_prior:\n");
	for (uint32_t resource_id_0 = 0; resource_id_0 < GetIntResourceCount(); ++resource_id_0) {
		for (uint32_t resource_id_1 = 0; resource_id_1 < GetIntResourceCount(); ++resource_id_1) {
			printf("%d ", m_resource_not_prior_relation.GetRelation(resource_id_0, resource_id_1));
		}
		printf("\n");
	}

	printf("Correct pass_resource_not_prior:\n");
	for (uint32_t pass_order = 0; pass_order < ordered_pass_graph.nodes.size(); ++pass_order) {
		for (uint32_t resource_id = 0; resource_id < GetIntResourceCount(); ++resource_id) {
			bool not_prior = false;
			for (auto &ref : GetIntResourceInfo(resource_id).references) {
				if (!IsPassPrior(pass_order, GetPassOrder(ref.pass)))
					not_prior = true;
			}
			printf("%d ", not_prior);
		}
		printf("\n");
	}

	RelationMatrix pass_resource_not_prior_relation;

	printf("X pass_resource_not_prior:\n");
	{
		pass_resource_not_prior_relation.Reset(ordered_pass_graph.nodes.size(), GetIntResourceCount());
		for (uint32_t pass_order = 0; pass_order < ordered_pass_graph.nodes.size(); ++pass_order) {
			ordered_pass_graph.nodes[pass_order].pass->for_each_input(
			    [this, pass_order, &pass_resource_not_prior_relation, &ordered_pass_graph](const Input *p_input) {
				    uint32_t internal_resource_id = GetIntResourceID(p_input->GetResource());
				    if (~internal_resource_id) {
					    for (uint32_t i = 0; i < ordered_pass_graph.nodes.size(); ++i) {
						    if (m_pass_prior_relation.GetRelation(pass_order, i))
							    pass_resource_not_prior_relation.SetRelation(i, internal_resource_id);
					    }
					    pass_resource_not_prior_relation.SetRelation(pass_order, internal_resource_id);
				    }
			    });
			for (const auto *p_edge : ordered_pass_graph.nodes[pass_order].output_edges) {
				if (p_edge->is_extra)
					continue;
				if (~p_edge->pass_to)
					pass_resource_not_prior_relation.ApplyRelations(pass_order, p_edge->pass_to);
			}
		}
	}
	for (uint32_t pass_order = 0; pass_order < ordered_pass_graph.nodes.size(); ++pass_order) {
		for (uint32_t resource_id = 0; resource_id < GetIntResourceCount(); ++resource_id) {
			printf("%d ", pass_resource_not_prior_relation.GetRelation(pass_order, resource_id));
		}
		printf("\n");
	}
}

void RenderGraphResolver::_extract_passes(const RenderGraphResolver::OrderedPassGraph &ordered_pass_graph) {
	const uint32_t kOrderedPassCount = ordered_pass_graph.nodes.size();
	std::vector<uint32_t> merge_length = ordered_pass_graph.compute_ordered_pass_merge_length();
	for (uint32_t i = 0, prev_length = 0; i < kOrderedPassCount; ++i) {
		const PassBase *pass = ordered_pass_graph.nodes[i].pass;
		auto &length = merge_length[i];
		if (length > prev_length)
			length = prev_length + 1;
		else
			length = pass->m_p_attachment_data ? 1 : 0;

		if (length <= 1) {
			pass->m_internal_info.pass_id = m_passes.size();
			pass->m_internal_info.subpass_id = 0;
			m_passes.emplace_back();
			m_passes.back().subpasses.push_back({pass});
			m_passes.back().is_render_pass = length;
		} else {
			pass->m_internal_info.pass_id = m_passes.size() - 1;
			pass->m_internal_info.subpass_id = length - 1;
			m_passes.back().subpasses.push_back({pass});
		}

		prev_length = length;
	}
}

void RenderGraphResolver::_extract_pass_attachments() {
	for (auto &pass_info : m_passes) {
		const auto register_attachment = [&pass_info](const ImageBase *image) {
			if (pass_info.attachment_id_map.find(image) == pass_info.attachment_id_map.end()) {
				uint32_t id = pass_info.attachment_id_map.size();
				pass_info.attachment_id_map[image] = id;
			}
		};

		for (const auto &subpass_info : pass_info.subpasses) {
			subpass_info.pass->for_each_input([&register_attachment](const Input *p_input) {
				if (UsageIsAttachment(p_input->GetUsage())) {
					p_input->GetResource()->Visit([&register_attachment](const auto *image) {
						if constexpr (ResourceVisitorTrait<decltype(image)>::kType == ResourceType::kImage) {
							if constexpr (ResourceVisitorTrait<decltype(image)>::kIsAlias)
								register_attachment(image->GetPointedResource());
							else
								register_attachment(image);
						} else
							assert(false);
					});
				}
			});
		}
	}
}

void RenderGraphResolver::_extract_dependencies_and_resource_validations(
    const RenderGraphResolver::OrderedPassGraph &ordered_pass_graph) {
	std::vector<std::unordered_map<const ResourceBase *, PassDependency *>> input_dep_maps(m_passes.size()),
	    output_dep_maps(m_passes.size());
	std::unordered_map<const ResourceBase *, PassDependency *> src_output_dep_map, dst_input_dep_map;

	const auto maintain_dependency = [this, &input_dep_maps, &output_dep_maps, &src_output_dep_map,
	                                  &dst_input_dep_map](const ResourceBase *resource, const DependencyLink &link_from,
	                                                      const DependencyLink &link_to) {
		if (link_from.p_input == nullptr) {
			// Mark as resource validation
			assert(link_to.pass && link_to.p_input);
			if (link_to.pass && link_to.p_input)
				m_passes[GetPassID(link_to.pass)].subpasses[GetSubpassID(link_to.pass)].validate_resources.push_back(
				    {resource, link_to.p_input});
			return;
		}

		auto &dep_map_from = link_from.pass ? output_dep_maps[GetPassID(link_from.pass)] : src_output_dep_map,
		     dep_map_to = link_to.pass ? input_dep_maps[GetPassID(link_to.pass)] : dst_input_dep_map;

		auto it_from = dep_map_from.find(resource), it_to = dep_map_to.find(resource);
		assert(it_from == dep_map_from.end() || it_to == dep_map_to.end());

		PassDependency *p_dep;
		if (it_from != dep_map_from.end()) {
			p_dep = it_from->second;
			p_dep->to.push_back(link_to);
			dep_map_to[resource] = p_dep;
		} else if (it_to != dep_map_to.end()) {
			p_dep = it_to->second;
			p_dep->from.push_back(link_from);
			dep_map_from[resource] = p_dep;
		} else {
			m_pass_dependencies.push_back({resource, {link_from}, {link_to}});
			p_dep = &m_pass_dependencies.back();
			dep_map_from[resource] = p_dep;
			dep_map_to[resource] = p_dep;
		}
	};

	m_pass_dependencies.reserve(ordered_pass_graph.edges.size()); // Ensure the pointers are valid

	for (auto &edge : ordered_pass_graph.edges) {
		if (edge.is_extra || edge.is_image_read_to_write)
			continue;
		// Add Dependencies
		const PassBase *pass_from = ~edge.pass_from ? ordered_pass_graph.nodes[edge.pass_from].pass : nullptr;
		const PassBase *pass_to = ~edge.pass_to ? ordered_pass_graph.nodes[edge.pass_to].pass : nullptr;

		if (pass_from && pass_to && GetPassID(pass_from) == GetPassID(pass_to)) {
			assert(GetSubpassID(pass_from) != GetSubpassID(pass_to));
			assert(UsageIsAttachment(edge.p_input_from->GetUsage()) && UsageIsAttachment(edge.p_input_to->GetUsage()));
			m_passes[GetPassID(pass_to)].subpass_dependencies.push_back(
			    {edge.resource, {edge.p_input_from, pass_from}, {edge.p_input_to, pass_to}});
		} else
			maintain_dependency(edge.resource, {edge.p_input_from, pass_from}, {edge.p_input_to, pass_to});
	}
}

void RenderGraphResolver::_sort_and_insert_image_dependencies() {
	uint32_t origin_dependency_count = m_pass_dependencies.size();
	for (uint32_t i = 0; i < origin_dependency_count; ++i) {
		auto &dep = m_pass_dependencies[i];
		// WARNING: dep might be invalid after push_back()

		assert(dep.from.size() == 1 && !dep.to.empty());

		if (dep.to.size() == 1)
			continue;

		// Sort the outputs and cull the useless ones
		std::sort(dep.to.begin(), dep.to.end(), [](const DependencyLink &l, const DependencyLink &r) {
			uint32_t l_pass_id = l.pass ? GetPassID(l.pass) : -1;
			uint32_t r_pass_id = r.pass ? GetPassID(r.pass) : -1;
			return !r.p_input || l_pass_id < r_pass_id;
		});
		while (!dep.to.empty() && dep.to.back().p_input == nullptr)
			dep.to.pop_back();

		if (dep.resource->GetType() == ResourceType::kImage) {
			std::vector<DependencyLink> links = std::move(dep.to);
			dep.to.clear();

			PassDependency *p_cur_dep = &dep;

			for (const auto &link : links) {
				if (link.p_input && !p_cur_dep->to.empty()) {
					const auto &prev_link = p_cur_dep->to.back();
					bool prev_is_attachment = UsageIsAttachment(prev_link.p_input->GetUsage());
					bool image_layout_changed = UsageGetImageLayout(prev_link.p_input->GetUsage()) !=
					                            UsageGetImageLayout(link.p_input->GetUsage());
					bool is_write_or_result = !UsageIsReadOnly(link.p_input->GetUsage());
					assert(!is_write_or_result || link.pass == links.back().pass);
					if (prev_is_attachment || image_layout_changed || is_write_or_result) {
						m_pass_dependencies.push_back({p_cur_dep->resource, p_cur_dep->to});
						p_cur_dep = &m_pass_dependencies.back();
					}
				}
				p_cur_dep->to.push_back(link);
				if (!link.p_input) {
					assert(link.pass == nullptr && links.size() == 1);
					break;
				}
			}
		}
	}
}

void RenderGraphResolver::extract_passes_and_dependencies(OrderedPassGraph &&ordered_pass_graph) {
	m_passes.clear();
	m_pass_dependencies.clear();

	_extract_passes(ordered_pass_graph);
	_extract_dependencies_and_resource_validations(ordered_pass_graph);
	_sort_and_insert_image_dependencies();
	_extract_pass_attachments();
}

void RenderGraphResolver::extract_resource_transient_info() {
	// TODO: finish this, set is_transient and VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT
}

void RenderGraphResolver::Resolve(const RenderGraphBase *p_render_graph) {
	Graph graph = make_graph(p_render_graph);
	extract_resources(graph);

	OrderedPassGraph ordered_pass_graph = make_ordered_pass_graph(std::move(graph));
	extract_pass_relation(ordered_pass_graph);
	extract_resource_relation(ordered_pass_graph);
	extract_resource_info(ordered_pass_graph);

	extract_passes_and_dependencies(std::move(ordered_pass_graph));
	if (p_render_graph->m_lazy_allocation_supported)
		extract_resource_transient_info();

	printf("\nResolved Passes: \n");
	for (const auto &pass_info : m_passes) {
		printf("PASS #%u (isRenderPass = %d): ", GetPassID(pass_info.subpasses[0].pass), pass_info.is_render_pass);
		for (const auto &subpass_info : pass_info.subpasses)
			std::cout << subpass_info.pass->GetKey().GetName() << ":" << subpass_info.pass->GetKey().GetID() << ", ";
		printf("\n");
		if (pass_info.is_render_pass) {
			printf("PASS_ATTACHMENTS: ");
			for (const auto &attachment : pass_info.attachment_id_map)
				std::cout << attachment.first->GetKey().GetName() << ":" << attachment.first->GetKey().GetID()
				          << " id = " << attachment.second << ", ";
			printf("\n");
		}
	}
	printf("\n");
}

} // namespace myvk_rg::_details_
