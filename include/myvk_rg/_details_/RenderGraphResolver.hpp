#ifndef MYVK_RG_RENDER_GRAPH_RESOLVER_HPP
#define MYVK_RG_RENDER_GRAPH_RESOLVER_HPP

#include <cinttypes>
#include <vector>

#include "Pass.hpp"
#include "ResourceBase.hpp"

namespace myvk_rg::_details_ {

class ResourceBase;
class PassBase;
class RenderGraphBase;
class ImageBase;
class CombinedImage;
class ManagedBuffer;
class Input;

class RenderGraphResolver {
private:
	class RelationMatrix {
	private:
		uint32_t m_size_r{};
		std::vector<uint64_t> m_bit_matrix;

	public:
		inline void Reset(uint32_t count_l, uint32_t count_r) {
			m_size_r = (count_r >> 6u) + ((count_r & 0x3f) ? 1u : 0);
			m_bit_matrix.clear();
			m_bit_matrix.resize(count_l * m_size_r);
		}
		inline void SetRelation(uint32_t l, uint32_t r) {
			m_bit_matrix[l * m_size_r + (r >> 6u)] |= (1ull << (r & 0x3fu));
		}
		inline void ApplyRelations(uint32_t l_from, uint32_t l_to) {
			for (uint32_t i = 0; i < m_size_r; ++i)
				m_bit_matrix[l_to * m_size_r + i] |= m_bit_matrix[l_from * m_size_r + i];
		}
		inline void ApplyRelations(const RelationMatrix &src_matrix, uint32_t l_from, uint32_t l_to) {
			assert(m_size_r == src_matrix.m_size_r);
			for (uint32_t i = 0; i < m_size_r; ++i)
				m_bit_matrix[l_to * m_size_r + i] |= src_matrix.m_bit_matrix[l_from * m_size_r + i];
		}
		inline bool GetRelation(uint32_t l, uint32_t r) const {
			return m_bit_matrix[l * m_size_r + (r >> 6u)] & (1ull << (r & 0x3fu));
		}
	};
	struct Graph; // The Graph Containing Passes and Internal Resources
	struct OrderedPassGraph;
	struct GroupedPassGraph {};

	struct InternalResourceInfo {};
	struct InternalBufferInfo : public InternalResourceInfo {
		const ManagedBuffer *buffer{};
		VkDeviceSize size{};
	};
	struct InternalImageInfo : public InternalResourceInfo {
		const ImageBase *image{};
		// uint32_t image_view_id;
	};
	struct InternalImageViewInfo {
		const ImageBase *image{};
		// uint32_t image_id;
		bool has_parent{};
	};
	struct PassInfo {
		const PassBase *pass{};
		uint32_t render_pass_id{};
	};
	struct RenderPassInfo {};

	std::vector<InternalImageInfo> m_internal_images;
	std::vector<InternalImageViewInfo> m_internal_image_views;
	std::vector<InternalBufferInfo> m_internal_buffers;

	RelationMatrix m_image_view_parent_relation, m_resource_conflicted_relation;

	static void _visit_resource_dep_passes(Graph *p_graph, const ResourceBase *resource, const PassBase *pass = nullptr,
	                                       const Input *p_input = nullptr);
	static void _insert_write_after_read_edges(Graph *p_graph);
	static Graph make_graph(const RenderGraphBase *p_render_graph);
	static void _initialize_combined_image(const CombinedImage *image);
	void extract_resources(const Graph &graph);
	static OrderedPassGraph make_ordered_pass_graph(Graph &&graph);
	// static RelationMatrix _extract_transitive_closure(const OrderedPassGraph &ordered_pass_graph);
	void initialize_naive_resource_relation(const OrderedPassGraph &ordered_pass_graph);

public:
	void Resolve(const RenderGraphBase *p_render_graph);
	// inline uint32_t GetPassCount() const { return m_passes.size(); }
	inline uint32_t GetInternalBufferCount() const { return m_internal_buffers.size(); }
	inline uint32_t GetInternalImageCount() const { return m_internal_images.size(); }
	inline uint32_t GetInternalResourceCount() const { return GetInternalBufferCount() + GetInternalImageCount(); }
	inline uint32_t GetInternalImageViewCount() const { return m_internal_image_views.size(); }

	inline static uint32_t GetInternalBufferID(const ManagedBuffer *buffer) {
		return buffer->m_internal_info.buffer_id;
	}
	inline static uint32_t GetInternalImageViewID(const ImageBase *image) {
		return image->Visit([](const auto *image) -> uint32_t {
			if constexpr (ResourceVisitorTrait<decltype(image)>::kIsInternal)
				return image->m_internal_info.image_view_id;
			else if constexpr (ResourceVisitorTrait<decltype(image)>::kIsAlias)
				return GetInternalImageViewID(image->GetPointedResource());
			else
				return -1;
		});
	}
	inline static uint32_t GetInternalImageViewID(const CombinedImage *image) {
		return image->m_internal_info.image_view_id;
	}
	inline static uint32_t GetInternalImageViewID(const ManagedImage *image) {
		return image->m_internal_info.image_view_id;
	}
	inline static uint32_t GetInternalImageID(const ImageBase *image) {
		return image->Visit([](const auto *image) -> uint32_t {
			if constexpr (ResourceVisitorTrait<decltype(image)>::kIsInternal)
				return image->m_internal_info.image_id;
			else if constexpr (ResourceVisitorTrait<decltype(image)>::kIsAlias)
				return GetInternalImageID(image->GetPointedResource());
			else
				return -1;
		});
	}
	inline static uint32_t GetInternalImageID(const CombinedImage *image) { return image->m_internal_info.image_id; }
	inline static uint32_t GetInternalImageID(const ManagedImage *image) { return image->m_internal_info.image_id; }

	inline uint32_t GetInternalResourceID(const ManagedBuffer *buffer) {
		return GetInternalBufferID(buffer) + GetInternalImageCount();
	}
	inline static uint32_t GetInternalResourceID(const CombinedImage *image) { return GetInternalImageID(image); }
	inline static uint32_t GetInternalResourceID(const ManagedImage *image) { return GetInternalImageID(image); }
	inline static uint32_t GetInternalResourceID(const ImageBase *image) { return GetInternalImageID(image); }
	inline uint32_t GetInternalResourceID(const ResourceBase *resource) {
		return resource->Visit([this](const auto *resource) -> uint32_t {
			if constexpr (ResourceVisitorTrait<decltype(resource)>::kIsInternal)
				return GetInternalResourceID(resource);
			else if constexpr (ResourceVisitorTrait<decltype(resource)>::kIsAlias)
				return GetInternalResourceID(resource->GetPointedResource());
			else
				return -1;
		});
	}

	inline static uint32_t GetPassID(const PassBase *pass) { return pass->m_internal_info.ordered_pass_id; }

	inline bool IsParentInternalImageView(uint32_t parent_image_view, uint32_t cur_image_view) const {
		return m_image_view_parent_relation.GetRelation(parent_image_view, cur_image_view);
	}
	inline bool IsConflictedInternalResources(uint32_t resource_0, uint32_t resource_1) const {
		return m_resource_conflicted_relation.GetRelation(resource_0, resource_1);
	}
};

} // namespace myvk_rg::_details_

#endif
