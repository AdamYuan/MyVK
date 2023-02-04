#ifndef MYVK_RG_RENDER_GRAPH_ALLOCATOR_HPP
#define MYVK_RG_RENDER_GRAPH_ALLOCATOR_HPP

#include <myvk_rg/_details_/Resource.hpp>

#include <myvk/BufferBase.hpp>
#include <myvk/ImageBase.hpp>
#include <myvk/ImageView.hpp>

#include "Bitset.hpp"
#include "RenderGraphResolver.hpp"

namespace myvk_rg::_details_ {

class RenderGraphAllocation;

class RenderGraphAllocator {
public:
	struct IntResourceAlloc {
		VkMemoryRequirements vk_memory_requirements{};
		VkDeviceSize memory_offset{};
		uint32_t allocation_id{};

	protected:
		uint32_t internal_resource_id{};
		const RenderGraphResolver::IntResourceInfo *p_info{};
		friend class RenderGraphAllocator;
	};
	struct IntImageAlloc final : public IntResourceAlloc {
		myvk::Ptr<myvk::ImageBase> myvk_image{};

		VkImageUsageFlags vk_image_usages{};
		VkImageType vk_image_type{VK_IMAGE_TYPE_2D};
		const SubImageSize *p_size{};
		bool persistence{};

	protected:
		const RenderGraphResolver::IntImageInfo &GetImageInfo() const {
			return *static_cast<const RenderGraphResolver::IntImageInfo *>(p_info);
		}
		friend class RenderGraphAllocator;
	};
	struct IntBufferAlloc final : public IntResourceAlloc {
		myvk::Ptr<myvk::BufferBase> myvk_buffer{};

		VkBufferUsageFlags vk_buffer_usages{};

	protected:
		const RenderGraphResolver::IntBufferInfo &GetBufferInfo() const {
			return *static_cast<const RenderGraphResolver::IntBufferInfo *>(p_info);
		}
		friend class RenderGraphAllocator;
	};
	struct IntImageViewAlloc {
		myvk::Ptr<myvk::ImageView> myvk_image_view{};
		SubImageSize size{};
		uint32_t base_layer{};
	};

private:
	const RenderGraphBase *m_p_render_graph;
	const RenderGraphResolver *m_p_resolved;

	std::vector<IntImageAlloc> m_allocated_images;
	std::vector<IntBufferAlloc> m_allocated_buffers;
	std::vector<IntImageViewAlloc> m_allocated_image_views;

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

	RelationMatrix m_allocated_resource_aliased_relation;

	void reset_resource_vectors();
	void _maintain_combined_image(const CombinedImage *image);
	void _accumulate_combined_image_base_layer(const CombinedImage *image);
	void update_resource_info();
	void create_vk_resources();
	void create_vk_image_views();
	void _make_naive_allocation(MemoryInfo &&memory_info, const VmaAllocationCreateInfo &allocation_create_info);
	void _make_optimal_allocation(MemoryInfo &&memory_info, const VmaAllocationCreateInfo &allocation_create_info);
	void create_and_bind_allocations();

public:
	void Allocate(const RenderGraphBase *p_render_graph, const RenderGraphResolver &resolved);

	inline const IntImageAlloc &GetIntImageAlloc(uint32_t image_id) const { return m_allocated_images[image_id]; }
	inline const std::vector<IntImageAlloc> &GetIntImageAllocVector() const { return m_allocated_images; }

	inline const IntBufferAlloc &GetIntBufferAlloc(uint32_t buffer_id) const { return m_allocated_buffers[buffer_id]; }
	inline const std::vector<IntBufferAlloc> &GetIntBufferAllocVector() const { return m_allocated_buffers; }

	inline const IntImageViewAlloc &GetIntImageViewAlloc(uint32_t image_view_id) const {
		return m_allocated_image_views[image_view_id];
	}
	inline const std::vector<IntImageViewAlloc> &GetIntImageViewAllocVector() const { return m_allocated_image_views; }

	inline const myvk::Ptr<myvk::ImageView> &GetVkImageView(const ManagedImage *image) const {
		return m_allocated_image_views[m_p_resolved->GetIntImageViewID(image)].myvk_image_view;
	}
	inline const myvk::Ptr<myvk::ImageView> &GetVkImageView(const CombinedImage *image) const {
		return m_allocated_image_views[m_p_resolved->GetIntImageViewID(image)].myvk_image_view;
	}
	inline static const myvk::Ptr<myvk::ImageView> &GetVkImageView(const ExternalImageBase *image) {
		return image->GetVkImageView();
	}
	inline const myvk::Ptr<myvk::ImageView> &GetVkImageView(const ImageAlias *image) const {
		return image->GetPointedResource()->Visit(
		    [this](const auto *image) -> const myvk::Ptr<myvk::ImageView> & { return GetVkImageView(image); });
	}
	inline const myvk::Ptr<myvk::ImageView> &GetVkImageView(const ImageBase *image) const {
		return image->Visit(
		    [this](const auto *image) -> const myvk::Ptr<myvk::ImageView> & { return GetVkImageView(image); });
	}

	inline const myvk::Ptr<myvk::ImageBase> &GetVkImage(const ManagedImage *image) const {
		return m_allocated_images[m_p_resolved->GetIntImageID(image)].myvk_image;
	}
	inline const myvk::Ptr<myvk::ImageBase> &GetVkImage(const CombinedImage *image) const {
		return m_allocated_images[m_p_resolved->GetIntImageID(image)].myvk_image;
	}
	inline static const myvk::Ptr<myvk::ImageBase> &GetVkImage(const ExternalImageBase *image) {
		return image->GetVkImageView()->GetImagePtr();
	}
	inline const myvk::Ptr<myvk::ImageBase> &GetVkImage(const ImageAlias *image) const {
		return image->GetPointedResource()->Visit(
		    [this](const auto *image) -> const myvk::Ptr<myvk::ImageBase> & { return GetVkImage(image); });
	}
	inline const myvk::Ptr<myvk::ImageBase> &GetVkImage(const ImageBase *image) const {
		return image->Visit(
		    [this](const auto *image) -> const myvk::Ptr<myvk::ImageBase> & { return GetVkImage(image); });
	}

	inline const myvk::Ptr<myvk::BufferBase> &GetVkBuffer(const ManagedBuffer *buffer) const {
		return m_allocated_buffers[m_p_resolved->GetIntBufferID(buffer)].myvk_buffer;
	}
	inline static const myvk::Ptr<myvk::BufferBase> &GetVkBuffer(const ExternalBufferBase *buffer) {
		return buffer->GetVkBuffer();
	}
	inline const myvk::Ptr<myvk::BufferBase> &GetVkBuffer(const BufferAlias *buffer) const {
		return buffer->GetPointedResource()->Visit(
		    [this](const auto *buffer) -> const myvk::Ptr<myvk::BufferBase> & { return GetVkBuffer(buffer); });
	}
	inline const myvk::Ptr<myvk::BufferBase> &GetVkBuffer(const BufferBase *buffer) const {
		return buffer->Visit(
		    [this](const auto *buffer) -> const myvk::Ptr<myvk::BufferBase> & { return GetVkBuffer(buffer); });
	}
};

} // namespace myvk_rg::_details_

#endif
