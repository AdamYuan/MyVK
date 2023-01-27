#ifndef MYVK_RG_RENDER_GRAPH_ALLOCATOR_HPP
#define MYVK_RG_RENDER_GRAPH_ALLOCATOR_HPP

#include <myvk_rg/_details_/Resource.hpp>

#include <myvk/BufferBase.hpp>
#include <myvk/ImageBase.hpp>
#include <myvk/ImageView.hpp>

#include "RenderGraphResolver.hpp"

namespace myvk_rg::_details_ {

class RenderGraphAllocation;
class RenderGraphImage;
class RenderGraphBuffer;

class RenderGraphAllocator {
public:
	struct IntResourceAlloc {
		VkMemoryRequirements vk_memory_requirements{};
		VkDeviceSize memory_offset{};
		uint32_t allocation_id{};

		uint32_t internal_resource_id{};
		const RenderGraphResolver::IntResourceInfo *p_info{};
	};
	struct IntImageAlloc final : public IntResourceAlloc {
		const RenderGraphResolver::IntImageInfo &GetImageInfo() const {
			return *static_cast<const RenderGraphResolver::IntImageInfo *>(p_info);
		}

		myvk::Ptr<RenderGraphImage> myvk_image{};
		const SubImageSize *p_size{};
		bool persistence{};
	};
	struct IntBufferAlloc final : public IntResourceAlloc {
		const RenderGraphResolver::IntBufferInfo &GetBufferInfo() const {
			return *static_cast<const RenderGraphResolver::IntBufferInfo *>(p_info);
		}

		myvk::Ptr<RenderGraphBuffer> myvk_buffer{};
	};
	struct IntImageViewAlloc {
		myvk::Ptr<myvk::ImageView> myvk_image_view{};
		SubImageSize size{};
		uint32_t base_layer{};
	};

private:
	const RenderGraphBase *m_p_render_graph;
	const RenderGraphResolver *m_p_resolved;

	std::vector<IntImageAlloc> m_internal_images;
	std::vector<IntBufferAlloc> m_internal_buffers;
	std::vector<IntImageViewAlloc> m_internal_image_views;

	struct AllocationInfo {
		myvk::Ptr<RenderGraphAllocation> myvk_allocation{};
	};
	struct MemoryInfo {
		std::vector<IntResourceAlloc *> resources;
		VkDeviceSize alignment = 1;
		uint32_t memory_type_bits = -1;
		inline void push(IntResourceAlloc *resource) {
			resources.push_back(resource);
			alignment = std::max(alignment, resource->vk_memory_requirements.alignment);
			memory_type_bits &= resource->vk_memory_requirements.memoryTypeBits;
		}
		inline bool empty() const { return resources.empty(); }
	};
	std::vector<AllocationInfo> m_allocations;

	void reset_resource_arrays();
	void _maintain_combined_image(const CombinedImage *image);
	void _accumulate_combined_image_base_layer(const CombinedImage *image);
	void update_image_info();
	void create_vk_resources();
	void create_vk_image_views();
	void _make_naive_allocation(MemoryInfo &&memory_info, const VmaAllocationCreateInfo &allocation_create_info);
	void _make_optimal_allocation(MemoryInfo &&memory_info, const VmaAllocationCreateInfo &allocation_create_info);
	void create_and_bind_allocations();

public:
	void Allocate(const RenderGraphBase *p_render_graph, const RenderGraphResolver &resolved);
};

} // namespace myvk_rg::_details_

#endif
