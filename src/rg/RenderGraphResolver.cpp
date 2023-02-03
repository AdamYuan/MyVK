#include "RenderGraphResolver.hpp"

#include <list>
#include <queue>
#include <unordered_map>
#include <unordered_set>

#include <iostream>

namespace myvk_rg::_details_ {

struct RenderGraphResolver::Graph {
	struct Edge : public SingleDependency {
		const PassBase *pass_from{}, *pass_to{};
		bool is_read_to_write{};
		mutable bool deleted{};
	};
	struct Node {
		std::vector<const Edge *> input_edges, output_edges;
		uint32_t in_degree{}, ordered_pass_id = -1;
	};
	std::unordered_map<const ImageBase *, bool> internal_image_set;
	std::unordered_set<const ManagedBuffer *> internal_buffer_set;
	std::unordered_map<const PassBase *, Node> nodes;
	std::list<Edge> edges;

	inline void add_edge(const PassBase *pass_from, const PassBase *pass_to, const ResourceBase *resource,
	                     const Input *p_input_from, const Input *p_input_to, bool is_read_to_write_edge) {
		// p_input_from == nullptr: Resource initial validation
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
	std::list<Edge> edges;
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
		for (uint32_t ordered_pass_id = 0; ordered_pass_id < kOrderedPassCount; ++ordered_pass_id) {
			auto &length = merge_length[ordered_pass_id];
			if (length <= 1)
				continue;
			for (auto *p_edge : nodes[ordered_pass_id].input_edges) {
				if (!UsageIsAttachment(p_edge->p_input_to->GetUsage()) ||
				    (p_edge->p_input_from && !UsageIsAttachment(p_edge->p_input_from->GetUsage()))) {
					// If an input dependency is not attachment, then all its producers can't be merged
					// Or an input dependency is attachment, but it is not produced as an attachment, then the producer
					// can't be merged
					length = std::min(length, ordered_pass_id - p_edge->pass_from);
				} else if (p_edge->p_input_from) {
					// If the input dependencies are both attachments
					assert(~p_edge->pass_from);
					length = std::min(length, ordered_pass_id - p_edge->pass_from + merge_length[p_edge->pass_from]);
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

		graph.nodes[pass].ordered_pass_id = ordered_pass_graph.nodes.size();
		ordered_pass_graph.nodes.push_back({pass});

		for (auto *p_edge : graph.nodes[pass].output_edges) {
			uint32_t degree = --graph.nodes[p_edge->pass_to].in_degree;
			if (degree == 0)
				candidate_queue.push(p_edge->pass_to);
		}
	}

	// ordered_pass_graph.edges.reserve(graph.edges.size());
	for (const auto &edge : graph.edges) {
		if (edge.deleted)
			continue;
		ordered_pass_graph.add_edge(graph.nodes[edge.pass_from].ordered_pass_id,
		                            graph.nodes[edge.pass_to].ordered_pass_id, edge.resource, edge.p_input_from,
		                            edge.p_input_to,
		                            edge.is_read_to_write && edge.resource->GetType() == ResourceType::kImage, false);
	}

	ordered_pass_graph.insert_extra_image_sequence_edges();

	return ordered_pass_graph;
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

void RenderGraphResolver::extract_basic_resource_relation(const OrderedPassGraph &ordered_pass_graph) {
	const uint32_t kOrderedPassCount = ordered_pass_graph.nodes.size();

	RelationMatrix pass_resource_not_prior_relation;
	{
		pass_resource_not_prior_relation.Reset(kOrderedPassCount, GetIntResourceCount());
		for (uint32_t ordered_pass_id = 0; ordered_pass_id < kOrderedPassCount; ++ordered_pass_id) {
			for (const auto *p_edge : ordered_pass_graph.nodes[ordered_pass_id].input_edges) {
				if (p_edge->is_extra)
					continue;
				if (~p_edge->pass_from)
					pass_resource_not_prior_relation.ApplyRelations(p_edge->pass_from, ordered_pass_id);
				ordered_pass_graph.nodes[ordered_pass_id].pass->for_each_input(
				    [this, ordered_pass_id, &pass_resource_not_prior_relation](const Input *p_input) {
					    uint32_t internal_resource_id = GetIntResourceID(p_input->GetResource());
					    if (~internal_resource_id)
						    pass_resource_not_prior_relation.SetRelation(ordered_pass_id, internal_resource_id);
				    });
			}
		}
	}

	RelationMatrix resource_not_prior_relation;
	{
		resource_not_prior_relation.Reset(GetIntResourceCount(), GetIntResourceCount());
		for (uint32_t ordered_pass_id = 0; ordered_pass_id < kOrderedPassCount; ++ordered_pass_id) {
			ordered_pass_graph.nodes[ordered_pass_id].pass->for_each_input(
			    [this, ordered_pass_id, &pass_resource_not_prior_relation,
			     &resource_not_prior_relation](const Input *p_input) {
				    uint32_t internal_resource_id = GetIntResourceID(p_input->GetResource());
				    if (~internal_resource_id)
					    resource_not_prior_relation.ApplyRelations(pass_resource_not_prior_relation, ordered_pass_id,
					                                               internal_resource_id);
			    });
		}
	}

	m_resource_conflicted_relation.Reset(GetIntResourceCount(), GetIntResourceCount());
	for (uint32_t resource_id_0 = 0; resource_id_0 < GetIntResourceCount(); ++resource_id_0) {
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

inline static void UpdateVkImageTypeFromVkImageViewType(VkImageType *p_image_type, VkImageViewType view_type) {
	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkImageViewCreateInfo.html
	switch (view_type) {
	case VK_IMAGE_VIEW_TYPE_1D:
	case VK_IMAGE_VIEW_TYPE_1D_ARRAY:
		*p_image_type = VK_IMAGE_TYPE_1D;
		return;
	case VK_IMAGE_VIEW_TYPE_CUBE:
	case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:
		*p_image_type = VK_IMAGE_TYPE_2D;
		return;
	case VK_IMAGE_VIEW_TYPE_2D:
	case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
		if (*p_image_type == VK_IMAGE_TYPE_1D)
			*p_image_type = VK_IMAGE_TYPE_2D;
		return;
	case VK_IMAGE_VIEW_TYPE_3D:
		*p_image_type = VK_IMAGE_TYPE_3D;
		return;
	default:
		return;
	}
}
void RenderGraphResolver::extract_resource_info(const RenderGraphResolver::OrderedPassGraph &ordered_pass_graph) {
	// extract vk_image_usages, vk_image_type, vk_buffer_usages, dependency_persistence, order_weight

	const auto update_resource_creation_info = [this](const auto *resource, const Input *p_input,
	                                                  uint32_t order) -> void {
		if constexpr (ResourceVisitorTrait<decltype(resource)>::kIsInternal) {
			if constexpr (ResourceVisitorTrait<decltype(resource)>::kType == ResourceType::kImage) {
				auto &image_info = m_internal_images[GetIntImageID(resource)];
				image_info.order_weight = std::min(image_info.order_weight, order);
				image_info.vk_image_usages |= UsageGetCreationUsages(p_input->GetUsage());
				UpdateVkImageTypeFromVkImageViewType(&image_info.vk_image_type, resource->GetViewType());
			} else {
				auto &buffer_info = m_internal_buffers[GetIntBufferID(resource)];
				buffer_info.order_weight = std::min(buffer_info.order_weight, order);
				buffer_info.vk_buffer_usages |= UsageGetCreationUsages(p_input->GetUsage());
			}
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
	for (uint32_t order = 0; order < ordered_pass_graph.nodes.size(); ++order) {
		const auto &node = ordered_pass_graph.nodes[order];
		node.pass->for_each_input(
		    [&update_resource_creation_info, &resource_set_persistent, order](const Input *p_input) -> void {
			    const auto local_update_resource_creation_info = [&update_resource_creation_info, p_input,
			                                                      order](const auto *resource) {
				    return update_resource_creation_info(resource, p_input, order);
			    };
			    p_input->GetResource()->Visit(
			        [&local_update_resource_creation_info, &resource_set_persistent](const auto *resource) {
				        if constexpr (ResourceVisitorTrait<decltype(resource)>::kIsAlias) {
					        if (resource->GetProducerPass() == nullptr)
						        resource->GetPointedResource()->Visit(resource_set_persistent);
					        resource->GetPointedResource()->Visit(local_update_resource_creation_info);
				        } else
					        local_update_resource_creation_info(resource);
			        });
		    });
	}
}

struct RenderGraphResolver::GroupedPassGraph {
	struct SubpassDependency : public SingleDependency {
		uint32_t subpass_from{}, subpass_to{};
	};
	struct SubpassInfo {
		const PassBase *pass{};
		std::vector<ResourceValidation> validate_resources;
	};
	struct PassDependency {
		struct Link {
			const Input *p_input{};
			uint32_t pass = -1, subpass = -1;
		};
		const ResourceBase *resource{};
		std::vector<Link> from, to;
	};
	struct AttachmentInfo {
		uint32_t attachment_id{};
		VkImageLayout initial_layout{VK_IMAGE_LAYOUT_UNDEFINED}, final_layout{VK_IMAGE_LAYOUT_UNDEFINED};
		bool is_initial{true}, is_final{true};
	};
	struct PassInfo {
		std::vector<SubpassInfo> subpasses;
		std::vector<SubpassDependency> subpass_dependencies;
		std::unordered_map<const ImageBase *, AttachmentInfo> attachments; // Attachment and its Info
		bool is_render_pass{};

		inline void maintain_attachment(const ImageBase *image, VkImageLayout initial_layout,
		                                VkImageLayout final_layout) {
			AttachmentInfo *p_info;
			{
				auto it = attachments.find(image);
				if (it == attachments.end()) {
					uint32_t id = attachments.size();
					p_info = &(attachments.insert({image, {id}}).first->second);
				} else
					p_info = &(it->second);
			}
			if (initial_layout) { // Not UNDEFINED
				assert(initial_layout == p_info->initial_layout);
				p_info->is_initial = false;
				p_info->initial_layout = initial_layout;
			}
			if (final_layout) { // Not UNDEFINED
				assert(final_layout == p_info->final_layout);
				p_info->is_final = false;
				p_info->final_layout = final_layout;
			}
		}
	};

	std::vector<PassInfo> passes;
	std::list<PassDependency> dependencies;

	inline void initialize_passes(const OrderedPassGraph &ordered_pass_graph) {
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
				pass->m_internal_info.pass_id = passes.size();
				pass->m_internal_info.subpass_id = 0;
				passes.emplace_back();
				passes.back().subpasses.push_back({pass});
				passes.back().is_render_pass = length;
			} else {
				pass->m_internal_info.pass_id = passes.size() - 1;
				pass->m_internal_info.subpass_id = length - 1;
				passes.back().subpasses.push_back({pass});
			}

			prev_length = length;
		}
	}

	inline void initialize_raw_dependencies(const OrderedPassGraph &ordered_pass_graph) {
		std::vector<std::unordered_map<const ResourceBase *, PassDependency *>> input_dep_maps(passes.size()),
		    output_dep_maps(passes.size());
		const auto maintain_dependency = [this, &input_dep_maps, &output_dep_maps](
		                                     const ResourceBase *resource, const PassDependency::Link &link_from,
		                                     const PassDependency::Link &link_to) {
			if (link_from.p_input == nullptr) {
				// Mark as resource validation
				assert(~link_to.pass && link_to.p_input);
				if (~link_to.pass && link_to.p_input)
					passes[link_to.pass].subpasses[link_to.subpass].validate_resources.push_back(
					    {resource, link_to.p_input});
				return;
			}

			PassDependency *p_dep;
			if (~link_from.pass && ~link_to.pass) {
				auto &dep_map_from = output_dep_maps[link_from.pass], dep_map_to = input_dep_maps[link_to.pass];
				auto it_from = dep_map_from.find(resource), it_to = dep_map_to.find(resource);
				assert(it_from == dep_map_from.end() || it_to == dep_map_to.end());

				if (it_from != dep_map_from.end()) {
					p_dep = it_from->second;
					p_dep->to.push_back(link_to);
					dep_map_to[resource] = p_dep;
				} else if (it_to != dep_map_to.end()) {
					p_dep = it_to->second;
					p_dep->from.push_back(link_from);
					dep_map_from[resource] = p_dep;
				} else {
					dependencies.push_back({resource, {link_from}, {link_to}});
					p_dep = &dependencies.back();
					dep_map_from[resource] = p_dep;
					dep_map_to[resource] = p_dep;
				}
			} else if (~link_from.pass) {
				auto &dep_map_from = output_dep_maps[link_from.pass];
				auto it_from = dep_map_from.find(resource);
				if (it_from != dep_map_from.end()) {
					p_dep = it_from->second;
					p_dep->to.push_back(link_to);
				} else {
					dependencies.push_back({resource, {link_from}, {link_to}});
					p_dep = &dependencies.back();
					dep_map_from[resource] = p_dep;
				}
			} else if (~link_to.pass) {
				auto dep_map_to = input_dep_maps[link_to.pass];
				auto it_to = dep_map_to.find(resource);
				if (it_to != dep_map_to.end()) {
					p_dep = it_to->second;
					p_dep->from.push_back(link_from);
				} else {
					dependencies.push_back({resource, {link_from}, {link_to}});
					p_dep = &dependencies.back();
					dep_map_to[resource] = p_dep;
				}
			}
		};

		for (auto &edge : ordered_pass_graph.edges) {
			if (edge.is_extra || edge.is_image_read_to_write)
				continue;
			// Add Dependencies
			if (~edge.pass_from && ~edge.pass_to) {
				const PassBase *pass_to = ordered_pass_graph.nodes[edge.pass_to].pass;
				uint32_t to_pass_id = pass_to->m_internal_info.pass_id;
				uint32_t to_subpass_id = pass_to->m_internal_info.subpass_id;

				const PassBase *pass_from = ordered_pass_graph.nodes[edge.pass_from].pass;
				uint32_t from_pass_id = pass_from->m_internal_info.pass_id;
				uint32_t from_subpass_id = pass_from->m_internal_info.subpass_id;

				if (from_pass_id == to_pass_id) {
					assert(UsageIsAttachment(edge.p_input_from->GetUsage()) &&
					       UsageIsAttachment(edge.p_input_to->GetUsage()));
					// Subpass Dependency
					passes[to_pass_id].subpass_dependencies.push_back(
					    {edge.resource, edge.p_input_from, edge.p_input_to, from_subpass_id, to_subpass_id});
				} else {
					// Pass Dependency
					maintain_dependency(edge.resource, {edge.p_input_from, from_pass_id, from_subpass_id},
					                    {edge.p_input_to, to_pass_id, to_subpass_id});
				}
			} else if (~edge.pass_from) {
				const PassBase *pass_from = ordered_pass_graph.nodes[edge.pass_from].pass;
				uint32_t from_pass_id = pass_from->m_internal_info.pass_id;
				uint32_t from_subpass_id = pass_from->m_internal_info.subpass_id;

				maintain_dependency(edge.resource, {edge.p_input_from, from_pass_id, from_subpass_id},
				                    {edge.p_input_to});
			} else if (~edge.pass_to) {
				const PassBase *pass_to = ordered_pass_graph.nodes[edge.pass_to].pass;
				uint32_t to_pass_id = pass_to->m_internal_info.pass_id;
				uint32_t to_subpass_id = pass_to->m_internal_info.subpass_id;

				maintain_dependency(edge.resource, {edge.p_input_from}, {edge.p_input_to, to_pass_id, to_subpass_id});
			}
		}
	}

	inline void sort_and_insert_image_dependencies() {
		auto dep_it = dependencies.begin();
		for (uint32_t i = 0, cnt = dependencies.size(); i++ < cnt; ++dep_it) {
			auto &dep = *dep_it;

			assert(dep.from.size() == 1 && !dep.to.empty());

			if (dep.to.size() == 1)
				continue;

			// Sort the outputs and cull the useless ones
			std::sort(dep.to.begin(), dep.to.end(), [](const PassDependency::Link &l, const PassDependency::Link &r) {
				return !r.p_input || l.pass < r.pass;
			});
			while (!dep.to.empty() && dep.to.back().p_input == nullptr)
				dep.to.pop_back();

			if (dep.resource->GetType() == ResourceType::kImage) {
				std::vector<PassDependency::Link> links = std::move(dep.to);
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
							dependencies.push_back({p_cur_dep->resource, p_cur_dep->to});
							p_cur_dep = &dependencies.back();
						}
					}
					p_cur_dep->to.push_back(link);
					if (!link.p_input) {
						assert(link.pass == -1 && links.size() == 1);
						break;
					}
				}
			}
		}
	}

	/* inline void initialize_attachment_and_cull_dependencies() {
		for (auto dep_it = dependencies.begin(); dep_it != dependencies.end();) {
			PassDependency &dep = *dep_it;

			if (dep.resource->GetType() != ResourceType::kImage) {
				++dep_it;
				continue;
			}
			auto *image = static_cast<const ImageBase *>(dep.resource);

			assert(!dep.from.empty() && !dep.to.empty());

			bool is_from_attachment =
			    dep.from.front().p_input && UsageIsAttachment(dep.from.front().p_input->GetUsage());
			bool is_to_attachment = dep.to.front().p_input && UsageIsAttachment(dep.to.front().p_input->GetUsage());

			assert(!is_from_attachment || dep.from.size() == 1);
			assert(!is_to_attachment || dep.to.size() == 1);

			if (is_from_attachment && is_to_attachment) {
				const auto &link_from = dep.from.front(), &link_to = dep.to.front();
				assert((~link_from.pass) || (~link_to.pass));

				VkImageLayout trans_layout = UsageGetImageLayout(link_from.p_input->GetUsage());
				if (~link_from.pass)
					passes[link_from.pass].maintain_attachment(image, VK_IMAGE_LAYOUT_UNDEFINED, trans_layout);
				if (~link_to.pass) {
					passes[link_to.pass].maintain_attachment(image, trans_layout, VK_IMAGE_LAYOUT_UNDEFINED);
					assert(~link_to.subpass);
					passes[link_to.pass].subpass_dependencies.push_back(
					    {image, link_from.p_input, link_to.p_input, VK_SUBPASS_EXTERNAL, link_to.subpass});
				}
			} else if (is_from_attachment) {
				const auto &link_from = dep.from.front();
				if (~link_from.pass) {
					for (const auto &link_to : dep.to) {
						passes[link_from.pass].maintain_attachment(
						    image, VK_IMAGE_LAYOUT_UNDEFINED,
						    link_to.p_input ? UsageGetImageLayout(link_to.p_input->GetUsage())
						                    : VK_IMAGE_LAYOUT_UNDEFINED);
						passes[link_from.pass].subpass_dependencies.push_back(
						    {image, link_from.p_input, link_to.p_input, link_from.subpass, VK_SUBPASS_EXTERNAL});
					}
				}
			} else if (is_to_attachment) {
				const auto &link_to = dep.to.front();
				if (~link_to.pass) {
					for (const auto &link_from : dep.from) {
						passes[link_to.pass].maintain_attachment(
						    image,
						    link_from.p_input ? UsageGetImageLayout(link_from.p_input->GetUsage())
						                      : VK_IMAGE_LAYOUT_UNDEFINED,
						    VK_IMAGE_LAYOUT_UNDEFINED);
						passes[link_to.pass].subpass_dependencies.push_back(
						    {image, link_from.p_input, link_to.p_input, VK_SUBPASS_EXTERNAL, link_to.subpass});
					}
				}
			}

			// Remove Attachment-Related Dependencies
			if (is_from_attachment || is_to_attachment)
				dep_it = dependencies.erase(dep_it);
			else
				++dep_it;
		}
	} */
};

RenderGraphResolver::GroupedPassGraph
RenderGraphResolver::make_grouped_pass_graph(OrderedPassGraph &&ordered_pass_graph) {
	GroupedPassGraph grouped_pass_graph{};

	grouped_pass_graph.initialize_passes(ordered_pass_graph);
	grouped_pass_graph.initialize_raw_dependencies(ordered_pass_graph);
	grouped_pass_graph.sort_and_insert_image_dependencies();
	// grouped_pass_graph.initialize_attachment_and_cull_dependencies();

	return grouped_pass_graph;
}

/* void RenderGraphResolver::_set_dependency_pointers() {
    for (const auto &dep : m_dependencies) {
        assert(std::is_sorted(
            dep.to.begin(), dep.to.end(),
            [](const PassDependency::Link &l, const PassDependency::Link &r) { return l.pass < r.pass; }));
        auto pass = dep.to.front().pass;
        (~pass ? m_passes[pass].prior_dependencies : m_post_dependencies).push_back(&dep);
    }
} */

void RenderGraphResolver::extract_passes_and_dependencies(OrderedPassGraph &&ordered_pass_graph) {
	m_passes.clear();
	m_pass_dependencies.clear();
	// m_passes.clear();
	// m_dependencies.clear();
	// m_post_dependencies.clear();
}

/* void RenderGraphResolver::extract_extra_resource_relation(const GroupedPassGraph &grouped_pass_graph) {
    // Avoid Memory Alias Barriers to subpass from previous passes
    for (const auto &dep : grouped_pass_graph.dependencies) {
        uint32_t int_resource_id = GetIntResourceID(dep.resource);
        if (int_resource_id == -1)
            continue;

        assert(!dep.from.empty() && !dep.to.empty());
        assert(!dep.to.front().p_input ||
               !UsageIsAttachment(dep.to.front().p_input->GetUsage())); // All attachment related dependencies should
                                                                        // have been deleted in _extract_attachments()

        for (const auto &link_to : dep.to) {
            if (~link_to.pass && grouped_pass_graph.passes[link_to.pass].is_render_pass)
                for (const auto &attachment_it : grouped_pass_graph.passes[link_to.pass].attachments) {
                    uint32_t int_attachment_resource_id = GetIntResourceID(attachment_it.first);
                    if (int_attachment_resource_id == -1)
                        continue;
                    m_resource_conflicted_relation.SetRelation(int_attachment_resource_id, int_resource_id);
                    m_resource_conflicted_relation.SetRelation(int_resource_id, int_attachment_resource_id);
                }
        }
    }
} */

void RenderGraphResolver::extract_resource_transient_info() {
	// TODO: finish this, set is_transient and VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT
}

void RenderGraphResolver::Resolve(const RenderGraphBase *p_render_graph) {
	Graph graph = make_graph(p_render_graph);
	extract_resources(graph);
	OrderedPassGraph ordered_pass_graph = make_ordered_pass_graph(std::move(graph));
	extract_basic_resource_relation(ordered_pass_graph);
	extract_resource_info(ordered_pass_graph);
	GroupedPassGraph grouped_pass_graph = make_grouped_pass_graph(std::move(ordered_pass_graph));
	extract_passes_and_dependencies(std::move(grouped_pass_graph));
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
			for (const auto &attachment : pass_info.attachments)
				std::cout << attachment.first->GetKey().GetName() << ":" << attachment.first->GetKey().GetID()
				          << " id = " << attachment.second.attachment_id << ", ";
			printf("\n");
		}
	}
	printf("\n");
}

} // namespace myvk_rg::_details_
