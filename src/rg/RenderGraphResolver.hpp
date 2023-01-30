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
	};
	struct PassDependency : public Dependency {
		uint32_t pass_from{}, pass_to{};
		// bool is_attachment_dependency{};
	};
	struct AttachmentInfo {
		uint32_t attachment_id{};
		VkImageLayout initial_layout{VK_IMAGE_LAYOUT_UNDEFINED}, final_layout{VK_IMAGE_LAYOUT_UNDEFINED};
		bool is_initial{true}, is_final{true};
	};
	struct PassInfo {
		std::vector<const PassDependency *> input_dependencies /*, output_dependencies*/;

		std::vector<SubpassInfo> subpasses;
		std::vector<SubpassDependency> subpass_dependencies;
		std::unordered_map<const ImageBase *, AttachmentInfo> attachments; // Attachment and its Info
		bool is_render_pass{};

	private:
		friend class RenderGraphResolver;
		inline void maintain_attachment(const ImageBase *image, const Input *p_initial_input,
		                                const Input *p_final_input) {
			AttachmentInfo *p_info;
			{
				auto it = attachments.find(image);
				if (it == attachments.end()) {
					uint32_t id = attachments.size();
					p_info = &(attachments.insert({image, {id}}).first->second);
				} else
					p_info = &(it->second);
			}
			if (p_initial_input) {
				assert(p_info->initial_layout == VK_IMAGE_LAYOUT_UNDEFINED ||
				       p_info->initial_layout == UsageGetImageLayout(p_initial_input->GetUsage()));
				p_info->is_initial = false;
				p_info->initial_layout = UsageGetImageLayout(p_initial_input->GetUsage());
			}
			if (p_final_input) {
				assert(p_info->final_layout == VK_IMAGE_LAYOUT_UNDEFINED ||
				       p_info->final_layout == UsageGetImageLayout(p_final_input->GetUsage()));
				p_info->is_final = false;
				p_info->final_layout = UsageGetImageLayout(p_final_input->GetUsage());
			}
		}
	};

private:
	struct Graph; // The Graph Containing Passes and Internal Resources
	struct OrderedPassGraph;

	std::vector<IntImageInfo> m_internal_images;
	std::vector<IntImageViewInfo> m_internal_image_views;
	std::vector<IntBufferInfo> m_internal_buffers;

	RelationMatrix /* m_image_view_parent_relation, */ m_resource_conflicted_relation;

	std::vector<PassInfo> m_passes;
	std::vector<PassDependency> m_dependencies;
	std::vector<const PassDependency *> m_post_dependencies;

	static void _visit_resource_dep_passes(Graph *p_graph, const ResourceBase *resource, const PassBase *pass,
	                                       const Input *p_input);
	static void _insert_write_after_read_edges(Graph *p_graph);
	static Graph make_graph(const RenderGraphBase *p_render_graph);
	static void _initialize_combined_image(const CombinedImage *image);
	void extract_resources(const Graph &graph);
	static void _insert_image_read_layout_edges(Graph *p_graph);
	static OrderedPassGraph make_ordered_pass_graph(Graph &&graph);
	// static RelationMatrix _extract_transitive_closure(const OrderedPassGraph &ordered_pass_graph);
	void extract_basic_resource_relation(const OrderedPassGraph &ordered_pass_graph);
	void extract_resource_info(const OrderedPassGraph &ordered_pass_graph);
	static std::vector<uint32_t> _compute_ordered_pass_merge_length(const OrderedPassGraph &ordered_pass_graph);
	void _add_merged_passes(const OrderedPassGraph &ordered_pass_graph);
	void _add_pass_dependencies_and_attachments(const OrderedPassGraph &ordered_pass_graph);
	void extract_passes_and_dependencies(OrderedPassGraph &&ordered_pass_graph);
	void extract_extra_resource_relation();
	void extract_resource_transient_info();

public:
	void Resolve(const RenderGraphBase *p_render_graph);

	inline uint32_t GetPassCount() const { return m_passes.size(); }
	inline const PassInfo &GetPassInfo(uint32_t pass_id) const { return m_passes[pass_id]; }
	inline static uint32_t GetPassID(const PassBase *pass) { return pass->m_internal_info.pass_id; }
	inline static uint32_t GetSubpassID(const PassBase *pass) { return pass->m_internal_info.subpass_id; }
	inline const std::vector<PassInfo> &GetPassInfoVector() const { return m_passes; }
	inline const std::vector<PassDependency> &GetDependencies() const { return m_dependencies; }
	inline const std::vector<const PassDependency *> &GetPostDependencyPtrs() const { return m_post_dependencies; }

	inline uint32_t GetIntBufferCount() const { return m_internal_buffers.size(); }
	inline const IntBufferInfo &GetIntBufferInfo(uint32_t buffer_id) const { return m_internal_buffers[buffer_id]; }
	inline const std::vector<IntBufferInfo> &GetIntBufferInfoVector() const { return m_internal_buffers; }

	inline uint32_t GetIntImageCount() const { return m_internal_images.size(); }
	inline const IntImageInfo &GetIntImageInfo(uint32_t image_id) const { return m_internal_images[image_id]; }
	inline const std::vector<IntImageInfo> &GetIntImageInfoVector() const { return m_internal_images; }

	inline uint32_t GetIntResourceCount() const { return GetIntBufferCount() + GetIntImageCount(); }
	/* inline const IntResourceInfo &GetIntResourceInfo(uint32_t resource_id) const {
	    return resource_id < GetIntImageCount()
	               ? (const IntResourceInfo &)GetIntImageInfo(resource_id)
	               : (const IntResourceInfo &)GetIntBufferInfo(resource_id - GetIntImageCount());
	} */

	inline uint32_t GetIntImageViewCount() const { return m_internal_image_views.size(); }
	inline const IntImageViewInfo &GetIntImageViewInfo(uint32_t image_view_id) const {
		return m_internal_image_views[image_view_id];
	}
	inline const std::vector<IntImageViewInfo> &GetIntImageViewInfoVector() const { return m_internal_image_views; }

	// Get Internal Buffer ID
	inline static uint32_t GetIntBufferID(const ManagedBuffer *buffer) { return buffer->m_internal_info.buffer_id; }
	inline static uint32_t GetIntBufferID(const ExternalBufferBase *) { return -1; }
	inline static uint32_t GetIntBufferID(const BufferAlias *buffer) {
		return buffer->GetPointedResource()->Visit(
		    [](const auto *buffer) -> uint32_t { return GetIntBufferID(buffer); });
	}
	inline static uint32_t GetIntBufferID(const BufferBase *buffer) {
		return buffer->Visit([](const auto *buffer) -> uint32_t { return GetIntBufferID(buffer); });
	}

	// Get Internal ImageView ID
	inline static uint32_t GetIntImageViewID(const CombinedImage *image) {
		return image->m_internal_info.image_view_id;
	}
	inline static uint32_t GetIntImageViewID(const ManagedImage *image) { return image->m_internal_info.image_view_id; }
	inline static uint32_t GetIntImageViewID(const ExternalImageBase *) { return -1; }
	inline static uint32_t GetIntImageViewID(const ImageAlias *image) {
		return image->GetPointedResource()->Visit(
		    [](const auto *image) -> uint32_t { return GetIntImageViewID(image); });
	}
	inline static uint32_t GetIntImageViewID(const ImageBase *image) {
		return image->Visit([](const auto *image) -> uint32_t { return GetIntImageViewID(image); });
	}

	// Get Internal Image ID
	inline static uint32_t GetIntImageID(const CombinedImage *image) { return image->m_internal_info.image_id; }
	inline static uint32_t GetIntImageID(const ManagedImage *image) { return image->m_internal_info.image_id; }
	inline static uint32_t GetIntImageID(const ExternalImageBase *image) { return -1; }
	inline static uint32_t GetIntImageID(const ImageAlias *image) {
		return image->GetPointedResource()->Visit([](const auto *image) -> uint32_t { return GetIntImageID(image); });
	}
	inline static uint32_t GetIntImageID(const ImageBase *image) {
		return image->Visit([](const auto *image) -> uint32_t { return GetIntImageID(image); });
	}

	// Get Internal Resource ID
	inline uint32_t GetIntResourceID(const ManagedBuffer *buffer) const {
		return GetIntBufferID(buffer) + GetIntImageCount();
	}
	inline static uint32_t GetIntResourceID(const ExternalBufferBase *buffer) { return -1; }
	inline uint32_t GetIntResourceID(const BufferAlias *buffer) const {
		return buffer->GetPointedResource()->Visit(
		    [this](const auto *buffer) -> uint32_t { return GetIntResourceID(buffer); });
	}
	inline uint32_t GetIntResourceID(const BufferBase *buffer) const {
		return buffer->Visit([this](const auto *buffer) -> uint32_t { return GetIntResourceID(buffer); });
	}
	inline static uint32_t GetIntResourceID(const ImageAlias *image) { return GetIntImageID(image); }
	inline static uint32_t GetIntResourceID(const CombinedImage *image) { return GetIntImageID(image); }
	inline static uint32_t GetIntResourceID(const ManagedImage *image) { return GetIntImageID(image); }
	inline static uint32_t GetIntResourceID(const ExternalImageBase *) { return -1; }
	inline static uint32_t GetIntResourceID(const ImageBase *image) { return GetIntImageID(image); }

	inline uint32_t GetIntResourceID(const ResourceBase *resource) const {
		return resource->Visit([this](const auto *resource) -> uint32_t { return GetIntResourceID(resource); });
	}

	/* inline bool IsParentIntImageView(uint32_t parent_image_view, uint32_t cur_image_view) const {
	    return m_image_view_parent_relation.GetRelation(parent_image_view, cur_image_view);
	} */
	inline bool IsIntResourcesConflicted(uint32_t resource_0, uint32_t resource_1) const {
		return m_resource_conflicted_relation.GetRelation(resource_0, resource_1);
	}
};

} // namespace myvk_rg::_details_

#endif
