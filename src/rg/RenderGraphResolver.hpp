#ifndef MYVK_RG_RENDER_GRAPH_RESOLVER_HPP
#define MYVK_RG_RENDER_GRAPH_RESOLVER_HPP

#include <cinttypes>
#include <vector>

#include "Bitset.hpp"

#include <myvk_rg/_details_/Pass.hpp>
#include <myvk_rg/_details_/RenderGraphBase.hpp>
#include <myvk_rg/_details_/Resource.hpp>

namespace myvk_rg::_details_ {

class RenderGraphResolver {
public:
	struct IntResourceInfo {
		uint32_t order_weight = -1;
		bool dependency_persistence{};
	};
	struct IntBufferInfo : public IntResourceInfo {
		const ManagedBuffer *buffer{};

		VkBufferUsageFlags vk_buffer_usages{};
	};
	struct IntImageInfo : public IntResourceInfo {
		const ImageBase *image{};
		// uint32_t image_view_id;
		VkImageUsageFlags vk_image_usages{};
		VkImageType vk_image_type{VK_IMAGE_TYPE_2D};
		bool is_transient{};
	};
	struct IntImageViewInfo {
		const ImageBase *image{};
		// uint32_t image_id;
		// bool has_parent{};
	};

	struct Dependency {
		const ResourceBase *resource{};
		const Input *p_input_from{}, *p_input_to{};
	};
	struct SubpassDependency : public Dependency {
		uint32_t subpass_from{}, subpass_to{};
	};
	struct SubpassInfo {
		const PassBase *pass{};
		std::vector<SubpassDependency> subpass_input_dependencies;
	};
	struct PassDependency : public Dependency {
		uint32_t pass_from{}, pass_to{};
	};
	struct PassInfo {
		std::vector<SubpassInfo> subpasses;
		std::vector<PassDependency> input_dependencies, output_dependencies;
		std::unordered_set<const ImageBase *> attachments; // Attachment and its ID
		bool is_render_pass{};
	};

private:
	struct Graph; // The Graph Containing Passes and Internal Resources
	struct OrderedPassGraph;

	std::vector<IntImageInfo> m_internal_images;
	std::vector<IntImageViewInfo> m_internal_image_views;
	std::vector<IntBufferInfo> m_internal_buffers;

	RelationMatrix /* m_image_view_parent_relation, */ m_resource_conflicted_relation;

	std::vector<PassInfo> m_passes;

	static void _visit_resource_dep_passes(Graph *p_graph, const ResourceBase *resource, const PassBase *pass = nullptr,
	                                       const Input *p_input = nullptr);
	static void _insert_write_after_read_edges(Graph *p_graph);
	static Graph make_graph(const RenderGraphBase *p_render_graph);
	static void _initialize_combined_image(const CombinedImage *image);
	void extract_resources(const Graph &graph);
	static OrderedPassGraph make_ordered_pass_graph(Graph &&graph);
	// static RelationMatrix _extract_transitive_closure(const OrderedPassGraph &ordered_pass_graph);
	void extract_basic_resource_relation(const OrderedPassGraph &ordered_pass_graph);
	void extract_resource_info(const OrderedPassGraph &ordered_pass_graph);
	static std::vector<uint32_t> _compute_ordered_pass_merge_length(const OrderedPassGraph &ordered_pass_graph);
	void _add_merged_passes(const OrderedPassGraph &ordered_pass_graph);
	void _add_pass_dependencies_and_attachments(const OrderedPassGraph &ordered_pass_graph);
	void extract_passes(OrderedPassGraph &&ordered_pass_graph);
	void extract_extra_resource_relation();
	void extract_resource_transient_info();

public:
	void Resolve(const RenderGraphBase *p_render_graph);

	inline uint32_t GetPassCount() const { return m_passes.size(); }
	inline const PassInfo &GetPassInfo(uint32_t pass_id) const { return m_passes[pass_id]; }
	inline static uint32_t GetPassID(const PassBase *pass) { return pass->m_internal_info.pass_id; }
	inline static uint32_t GetSubpassID(const PassBase *pass) { return pass->m_internal_info.subpass_id; }
	inline const std::vector<PassInfo> &GetPassInfoVector() const { return m_passes; }

	inline uint32_t GetIntBufferCount() const { return m_internal_buffers.size(); }
	inline const IntBufferInfo &GetIntBufferInfo(uint32_t buffer_id) const { return m_internal_buffers[buffer_id]; }
	inline uint32_t GetIntImageCount() const { return m_internal_images.size(); }
	inline const IntImageInfo &GetIntImageInfo(uint32_t image_id) const { return m_internal_images[image_id]; }
	inline uint32_t GetIntResourceCount() const { return GetIntBufferCount() + GetIntImageCount(); }
	inline const IntResourceInfo &GetIntResourceInfo(uint32_t resource_id) const {
		return resource_id < GetIntImageCount()
		           ? (const IntResourceInfo &)GetIntImageInfo(resource_id)
		           : (const IntResourceInfo &)GetIntBufferInfo(resource_id - GetIntImageCount());
	}
	inline uint32_t GetIntImageViewCount() const { return m_internal_image_views.size(); }
	inline const IntImageViewInfo &GetIntImageViewInfo(uint32_t image_view_id) const {
		return m_internal_image_views[image_view_id];
	}

	inline static uint32_t GetIntBufferID(const BufferBase *buffer) {
		return buffer->Visit([](const auto *buffer) -> uint32_t {
			if constexpr (ResourceVisitorTrait<decltype(buffer)>::kIsInternal)
				return buffer->m_internal_info.buffer_id;
			else if constexpr (ResourceVisitorTrait<decltype(buffer)>::kIsAlias)
				return GetIntBufferID(buffer->GetPointedResource());
			else
				return -1;
		});
	}
	inline static uint32_t GetIntBufferID(const ManagedBuffer *buffer) { return buffer->m_internal_info.buffer_id; }

	inline static uint32_t GetIntImageViewID(const ImageBase *image) {
		return image->Visit([](const auto *image) -> uint32_t {
			if constexpr (ResourceVisitorTrait<decltype(image)>::kIsInternal)
				return image->m_internal_info.image_view_id;
			else if constexpr (ResourceVisitorTrait<decltype(image)>::kIsAlias)
				return GetIntImageViewID(image->GetPointedResource());
			else
				return -1;
		});
	}
	inline static uint32_t GetIntImageViewID(const CombinedImage *image) {
		return image->m_internal_info.image_view_id;
	}
	inline static uint32_t GetIntImageViewID(const ManagedImage *image) { return image->m_internal_info.image_view_id; }

	inline static uint32_t GetIntImageID(const ImageBase *image) {
		return image->Visit([](const auto *image) -> uint32_t {
			if constexpr (ResourceVisitorTrait<decltype(image)>::kIsInternal)
				return image->m_internal_info.image_id;
			else if constexpr (ResourceVisitorTrait<decltype(image)>::kIsAlias)
				return GetIntImageID(image->GetPointedResource());
			else
				return -1;
		});
	}
	inline static uint32_t GetIntImageID(const CombinedImage *image) { return image->m_internal_info.image_id; }
	inline static uint32_t GetIntImageID(const ManagedImage *image) { return image->m_internal_info.image_id; }

	inline uint32_t GetIntResourceID(const BufferBase *buffer) const {
		return GetIntBufferID(buffer) + GetIntImageCount();
	}
	inline uint32_t GetIntResourceID(const ManagedBuffer *buffer) const {
		return GetIntBufferID(buffer) + GetIntImageCount();
	}
	inline static uint32_t GetIntResourceID(const ImageBase *image) { return GetIntImageID(image); }
	inline static uint32_t GetIntResourceID(const CombinedImage *image) { return GetIntImageID(image); }
	inline static uint32_t GetIntResourceID(const ManagedImage *image) { return GetIntImageID(image); }
	inline uint32_t GetIntResourceID(const ResourceBase *resource) const {
		return resource->Visit([this](const auto *resource) -> uint32_t {
			if constexpr (ResourceVisitorTrait<decltype(resource)>::kIsInternal)
				return GetIntResourceID(resource);
			else if constexpr (ResourceVisitorTrait<decltype(resource)>::kIsAlias)
				return GetIntResourceID(resource->GetPointedResource());
			else
				return -1;
		});
	}

	/* inline bool IsParentIntImageView(uint32_t parent_image_view, uint32_t cur_image_view) const {
	    return m_image_view_parent_relation.GetRelation(parent_image_view, cur_image_view);
	} */
	inline bool IsConflictedIntResources(uint32_t resource_0, uint32_t resource_1) const {
		return m_resource_conflicted_relation.GetRelation(resource_0, resource_1);
	}
};

} // namespace myvk_rg::_details_

#endif
