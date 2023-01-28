#include "RenderGraphAllocator.hpp"

#include <algorithm>
#include <iostream>

namespace myvk_rg::_details_ {

class RenderGraphImage final : public myvk::ImageBase {
private:
	const RenderGraphBase *m_render_graph_ptr;

public:
	inline RenderGraphImage(const RenderGraphBase *render_graph_ptr, const VkImageCreateInfo &create_info)
	    : m_render_graph_ptr{render_graph_ptr} {
		vkCreateImage(GetDevicePtr()->GetHandle(), &create_info, nullptr, &m_image);
		m_extent = create_info.extent;
		m_mip_levels = create_info.mipLevels;
		m_array_layers = create_info.arrayLayers;
		m_format = create_info.format;
		m_type = create_info.imageType;
		m_usage = create_info.usage;
	}
	inline ~RenderGraphImage() final {
		if (m_image != VK_NULL_HANDLE)
			vkDestroyImage(GetDevicePtr()->GetHandle(), m_image, nullptr);
	};
	const myvk::Ptr<myvk::Device> &GetDevicePtr() const final { return m_render_graph_ptr->GetDevicePtr(); }
};
class RenderGraphBuffer final : public myvk::BufferBase {
private:
	const RenderGraphBase *m_render_graph_ptr;

public:
	inline RenderGraphBuffer(const RenderGraphBase *render_graph_ptr, const VkBufferCreateInfo &create_info)
	    : m_render_graph_ptr{render_graph_ptr} {
		vkCreateBuffer(GetDevicePtr()->GetHandle(), &create_info, nullptr, &m_buffer);
		m_size = create_info.size;
	}
	inline ~RenderGraphBuffer() final {
		if (m_buffer != VK_NULL_HANDLE)
			vkDestroyBuffer(GetDevicePtr()->GetHandle(), m_buffer, nullptr);
	}
	const myvk::Ptr<myvk::Device> &GetDevicePtr() const final { return m_render_graph_ptr->GetDevicePtr(); }
};

class RenderGraphAllocation final : public myvk::DeviceObjectBase {
private:
	const RenderGraphBase *m_render_graph_ptr;
	VmaAllocation m_allocation{VK_NULL_HANDLE};

public:
	inline RenderGraphAllocation(const RenderGraphBase *render_graph_ptr,
	                             const VkMemoryRequirements &memory_requirements,
	                             const VmaAllocationCreateInfo &create_info)
	    : m_render_graph_ptr{render_graph_ptr} {
		vmaAllocateMemory(GetDevicePtr()->GetAllocatorHandle(), &memory_requirements, &create_info, &m_allocation,
		                  nullptr);
	}
	inline ~RenderGraphAllocation() final {
		if (m_allocation != VK_NULL_HANDLE)
			vmaFreeMemory(GetDevicePtr()->GetAllocatorHandle(), m_allocation);
	}
	inline VmaAllocation GetHandle() const { return m_allocation; }
	const myvk::Ptr<myvk::Device> &GetDevicePtr() const final { return m_render_graph_ptr->GetDevicePtr(); }
};

void RenderGraphAllocator::_maintain_combined_image(const CombinedImage *image) {
	// Visit Each Child Image, Update Size and Base Layer (Relative, need to be accumulated after)
	SubImageSize &cur_size = m_internal_image_views[m_p_resolved->GetIntImageViewID(image)].size;
	image->ForEachExpandedImage([this, &cur_size](auto *sub_image) -> void {
		if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kIsCombinedOrManagedImage) {
			// Merge the Size of the Current Child Image
			auto &sub_image_alloc = m_internal_image_views[m_p_resolved->GetIntImageViewID(sub_image)];
			if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kClass == ResourceClass::kManagedImage)
				sub_image_alloc.size = sub_image->GetSize();
			else if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kClass == ResourceClass::kCombinedImage)
				_maintain_combined_image(sub_image); // Further Query SubImage Size

			cur_size.Merge(sub_image_alloc.size);
			sub_image_alloc.base_layer = cur_size.GetArrayLayers() - sub_image_alloc.size.GetArrayLayers();
		}
	});
}

void RenderGraphAllocator::_accumulate_combined_image_base_layer(const CombinedImage *image) {
	uint32_t cur_base_layer = m_internal_image_views[m_p_resolved->GetIntImageViewID(image)].base_layer;
	image->ForEachExpandedImage([this, cur_base_layer](auto *sub_image) -> void {
		if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kIsCombinedOrManagedImage) {
			auto &sub_image_alloc = m_internal_image_views[m_p_resolved->GetIntImageViewID(sub_image)];
			sub_image_alloc.base_layer += cur_base_layer;
			if constexpr (ResourceVisitorTrait<decltype(sub_image)>::kClass == ResourceClass::kCombinedImage)
				_accumulate_combined_image_base_layer(sub_image);
		}
	});
}

void RenderGraphAllocator::update_image_info() {
	// Update Image Sizes and Base Layers
	for (auto &image_alloc : m_internal_images) {
		const auto &image_info = image_alloc.GetImageInfo();
		image_alloc.persistence = false;

		auto &image_view_alloc = m_internal_image_views[m_p_resolved->GetIntImageViewID(image_info.image)];
		image_view_alloc.base_layer = 0;

		image_info.image->Visit([this, &image_alloc, &image_view_alloc](auto *image) -> void {
			if constexpr (ResourceVisitorTrait<decltype(image)>::kIsInternal) {
				if constexpr (ResourceVisitorTrait<decltype(image)>::kClass == ResourceClass::kCombinedImage) {
					_maintain_combined_image(image);
					_accumulate_combined_image_base_layer(image);
				} else
					image_view_alloc.size = image->GetSize();
				image_alloc.p_size = &image_view_alloc.size;
			} else
				assert(false);
		});
	}
	// Update Image Persistence
	for (uint32_t image_view_id = 0; image_view_id < m_p_resolved->GetIntImageViewCount(); ++image_view_id) {
		const auto &image_view_info = m_p_resolved->GetIntImageViewInfo(image_view_id);
		image_view_info.image->Visit([this](const auto *image) {
			if constexpr (ResourceVisitorTrait<decltype(image)>::kClass == ResourceClass::kManagedImage) {
				m_internal_images[m_p_resolved->GetIntImageID(image)].persistence |= image->GetPersistence();
			}
		});
	}
}

void RenderGraphAllocator::create_vk_resources() {
	// Create Buffers
	for (auto &buffer_alloc : m_internal_buffers) {
		const auto &buffer_info = buffer_alloc.GetBufferInfo();
		VkBufferCreateInfo create_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
		create_info.usage = buffer_info.vk_buffer_usages;
		create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		create_info.size = buffer_info.buffer->GetSize();

		buffer_alloc.myvk_buffer = std::make_shared<RenderGraphBuffer>(m_p_render_graph, create_info);
		vkGetBufferMemoryRequirements(m_p_render_graph->GetDevicePtr()->GetHandle(),
		                              buffer_alloc.myvk_buffer->GetHandle(), &buffer_alloc.vk_memory_requirements);
	}

	// Create Images
	for (auto &image_alloc : m_internal_images) {
		const auto &image_info = image_alloc.GetImageInfo();
		assert(image_alloc.p_size && image_alloc.p_size->GetBaseMipLevel() == 0);

		VkImageCreateInfo create_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
		create_info.usage = image_info.vk_image_usages;
		if (image_info.is_transient)
			create_info.usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
		create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		create_info.format = image_info.image->GetFormat();
		create_info.samples = VK_SAMPLE_COUNT_1_BIT;
		create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
		create_info.imageType = image_info.vk_image_type;
		{ // Set Size Info
			VkExtent3D &extent = create_info.extent;
			extent = {1, 1, 1};
			switch (create_info.imageType) {
			case VK_IMAGE_TYPE_1D: {
				extent.width = image_alloc.p_size->GetExtent().width;
				create_info.mipLevels = image_alloc.p_size->GetMipLevels();
				create_info.arrayLayers = image_alloc.p_size->GetArrayLayers();
			} break;
			case VK_IMAGE_TYPE_2D: {
				extent.width = image_alloc.p_size->GetExtent().width;
				extent.height = image_alloc.p_size->GetExtent().height;
				create_info.mipLevels = image_alloc.p_size->GetMipLevels();
				create_info.arrayLayers = image_alloc.p_size->GetArrayLayers();
			} break;
			case VK_IMAGE_TYPE_3D: {
				assert(image_alloc.p_size->GetExtent().depth == 1 || image_alloc.p_size->GetArrayLayers() == 1);
				extent.width = image_alloc.p_size->GetExtent().width;
				extent.height = image_alloc.p_size->GetExtent().height;
				extent.depth = std::max(image_alloc.p_size->GetExtent().depth, image_alloc.p_size->GetArrayLayers());
				create_info.mipLevels = image_alloc.p_size->GetMipLevels();
				create_info.arrayLayers = 1;
			} break;
			default:
				assert(false);
			}
		}

		image_alloc.myvk_image = std::make_shared<RenderGraphImage>(m_p_render_graph, create_info);
		vkGetImageMemoryRequirements(m_p_render_graph->GetDevicePtr()->GetHandle(), image_alloc.myvk_image->GetHandle(),
		                             &image_alloc.vk_memory_requirements);
	}
}

inline static constexpr VkImageAspectFlags VkImageAspectFlagsFromVkFormat(VkFormat format) {
	switch (format) {
	case VK_FORMAT_D32_SFLOAT:
	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_X8_D24_UNORM_PACK32:
		return VK_IMAGE_ASPECT_DEPTH_BIT;
	case VK_FORMAT_D16_UNORM_S8_UINT:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	case VK_FORMAT_S8_UINT:
		return VK_IMAGE_ASPECT_STENCIL_BIT;
	default:
		return VK_IMAGE_ASPECT_COLOR_BIT;
	}
}
void RenderGraphAllocator::create_vk_image_views() {
	printf("\nImage Views: \n");
	for (uint32_t image_view_id = 0; image_view_id < m_p_resolved->GetIntImageViewCount(); ++image_view_id) {
		const auto *image = m_p_resolved->GetIntImageViewInfo(image_view_id).image;
		auto &image_view_alloc = m_internal_image_views[image_view_id];
		image->Visit([this, &image_view_alloc](const auto *image) {
			if constexpr (ResourceVisitorTrait<decltype(image)>::kIsInternal) {
				VkImageViewCreateInfo create_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
				create_info.format = image->GetFormat();
				create_info.viewType = image->GetViewType();
				create_info.subresourceRange.baseArrayLayer = image_view_alloc.base_layer;
				create_info.subresourceRange.layerCount = image_view_alloc.size.GetArrayLayers();
				create_info.subresourceRange.baseMipLevel = image_view_alloc.size.GetBaseMipLevel();
				create_info.subresourceRange.levelCount = image_view_alloc.size.GetMipLevels();
				create_info.subresourceRange.aspectMask = VkImageAspectFlagsFromVkFormat(image->GetFormat());

				uint32_t image_id = m_p_resolved->GetIntImageID(image);

				image_view_alloc.myvk_image_view =
				    myvk::ImageView::Create(m_internal_images[image_id].myvk_image, create_info);

				std::cout << image->GetKey().GetName() << ":" << image->GetKey().GetID();
				printf(" {baseArrayLayer, layerCount, baseMipLevel, levelCount} = {%u, %u, %u, %u}\n",
				       create_info.subresourceRange.baseArrayLayer, create_info.subresourceRange.layerCount,
				       create_info.subresourceRange.baseMipLevel, create_info.subresourceRange.levelCount);
			} else
				assert(false);
		});
	}
	printf("\n");
}

inline static constexpr VkDeviceSize DivRoundUp(VkDeviceSize l, VkDeviceSize r) { return (l / r) + (l % r ? 1 : 0); }

void RenderGraphAllocator::_make_naive_allocation(MemoryInfo &&memory_info,
                                                  const VmaAllocationCreateInfo &allocation_create_info) {
	if (memory_info.empty())
		return;

	uint32_t allocation_id = m_allocations.size();
	m_allocations.emplace_back();
	auto &allocation_info = m_allocations.back();

	VkMemoryRequirements memory_requirements = {};
	memory_requirements.alignment = memory_info.alignment;
	memory_requirements.memoryTypeBits = memory_info.memory_type_bits;

	VkDeviceSize allocation_blocks = 0;
	for (auto *p_resource_info : memory_info.resources) {
		p_resource_info->allocation_id = allocation_id;
		p_resource_info->memory_offset = allocation_blocks * memory_info.alignment;
		allocation_blocks += DivRoundUp(p_resource_info->vk_memory_requirements.size, memory_info.alignment);
	}
	memory_requirements.size = allocation_blocks * memory_info.alignment;

	allocation_info.myvk_allocation =
	    std::make_shared<RenderGraphAllocation>(m_p_render_graph, memory_requirements, allocation_create_info);
}

// An AABB indicates a placed resource
struct MemBlock {
	VkDeviceSize mem_begin, mem_end;
	uint32_t internal_resource_id;
};
struct MemEvent {
	VkDeviceSize mem;
	uint32_t cnt;
	inline bool operator<(const MemEvent &r) const { return mem < r.mem; }
};
void RenderGraphAllocator::_make_optimal_allocation(MemoryInfo &&memory_info,
                                                    const VmaAllocationCreateInfo &allocation_create_info) {
	if (memory_info.empty())
		return;

	uint32_t allocation_id = m_allocations.size();
	m_allocations.emplace_back();
	auto &allocation_info = m_allocations.back();

	// Sort Resources by required sizes, place large resources first
	std::sort(memory_info.resources.begin(), memory_info.resources.end(),
	          [](const IntResourceAlloc *l, const IntResourceAlloc *r) -> bool {
		          return l->vk_memory_requirements.size > r->vk_memory_requirements.size ||
		                 (l->vk_memory_requirements.size == r->vk_memory_requirements.size &&
		                  l->p_info->order_weight > r->p_info->order_weight);
	          });

	VkDeviceSize allocation_blocks = 0;
	{
		std::vector<MemBlock> blocks;
		std::vector<MemEvent> events;
		blocks.reserve(memory_info.resources.size());
		events.reserve(memory_info.resources.size() << 1u);

		for (auto *p_resource_info : memory_info.resources) {
			// Find an empty position to place
			events.clear();
			for (const auto &block : blocks)
				if (m_p_resolved->IsConflictedIntResources(p_resource_info->internal_resource_id,
				                                           block.internal_resource_id)) {
					events.push_back({block.mem_begin, 1});
					events.push_back({block.mem_end, (uint32_t)-1});
				}
			std::sort(events.begin(), events.end());

			VkDeviceSize required_mem_size =
			    DivRoundUp(p_resource_info->vk_memory_requirements.size, memory_info.alignment);

			VkDeviceSize optimal_mem_pos = 0, optimal_mem_size = std::numeric_limits<VkDeviceSize>::max();
			if (!events.empty()) {
				assert(events.front().cnt == 1 && events.back().cnt == -1);
				if (events.front().mem >= required_mem_size)
					optimal_mem_size = events.front().mem;
				else
					optimal_mem_pos = events.back().mem;

				for (uint32_t i = 1; i < events.size(); ++i) {
					events[i].cnt += events[i - 1].cnt;
					if (events[i - 1].cnt == 0 && events[i].cnt == 1) {
						VkDeviceSize cur_mem_pos = events[i - 1].mem, cur_mem_size = events[i].mem - events[i - 1].mem;
						if (required_mem_size <= cur_mem_size && cur_mem_size < optimal_mem_size) {
							optimal_mem_size = cur_mem_size;
							optimal_mem_pos = cur_mem_pos;
						}
					}
				}
			}

			p_resource_info->allocation_id = allocation_id;
			p_resource_info->memory_offset = optimal_mem_pos * memory_info.alignment;
			allocation_blocks = std::max(allocation_blocks, optimal_mem_pos + required_mem_size);

			blocks.push_back(
			    {optimal_mem_pos, optimal_mem_pos + required_mem_size, p_resource_info->internal_resource_id});
		}
	}

	VkMemoryRequirements memory_requirements = {};
	memory_requirements.alignment = memory_info.alignment;
	memory_requirements.memoryTypeBits = memory_info.memory_type_bits;
	memory_requirements.size = allocation_blocks * memory_info.alignment;

	allocation_info.myvk_allocation =
	    std::make_shared<RenderGraphAllocation>(m_p_render_graph, memory_requirements, allocation_create_info);
}

void RenderGraphAllocator::create_and_bind_allocations() {
	m_allocations.clear();
	{ // Create Allocations
		MemoryInfo device_memory{}, persistent_device_memory{}, lazy_memory{}, mapped_memory{};

		device_memory.resources.reserve(m_p_resolved->GetIntResourceCount());
		uint32_t buffer_image_granularity = m_p_render_graph->GetDevicePtr()
		                                        ->GetPhysicalDevicePtr()
		                                        ->GetProperties()
		                                        .vk10.limits.bufferImageGranularity;
		device_memory.alignment = persistent_device_memory.alignment = buffer_image_granularity;

		for (auto &image_alloc : m_internal_images) {
			const auto &image_info = image_alloc.GetImageInfo();
			if (image_info.is_transient)
				lazy_memory.push(&image_alloc); // If the image is Transient and LAZY_ALLOCATION is supported
			else if (image_info.dependency_persistence || image_alloc.persistence)
				persistent_device_memory.push(&image_alloc);
			else
				device_memory.push(&image_alloc);
		}
		for (auto &buffer_alloc : m_internal_buffers) {
			const auto &buffer_info = buffer_alloc.GetBufferInfo();
			if (false) // TODO: Mapped Buffer Condition
				mapped_memory.push(&buffer_alloc);
			else if (buffer_info.dependency_persistence || buffer_info.buffer->GetPersistence())
				persistent_device_memory.push(&buffer_alloc);
			else
				device_memory.push(&buffer_alloc);
		}
		{
			VmaAllocationCreateInfo create_info = {};
			create_info.flags = VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT;
			create_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			_make_optimal_allocation(std::move(device_memory), create_info);
		}
		{
			VmaAllocationCreateInfo create_info = {};
			// create_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
			create_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			_make_naive_allocation(std::move(persistent_device_memory), create_info);
		}
		{
			VmaAllocationCreateInfo create_info = {};
			// create_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
			create_info.usage = VMA_MEMORY_USAGE_GPU_LAZILY_ALLOCATED;
			_make_naive_allocation(std::move(lazy_memory), create_info);
		}
	}
	// Bind Memory
	for (auto &image_alloc : m_internal_images) {
		vmaBindImageMemory2(m_p_render_graph->GetDevicePtr()->GetAllocatorHandle(),
		                    m_allocations[image_alloc.allocation_id].myvk_allocation->GetHandle(),
		                    image_alloc.memory_offset, image_alloc.myvk_image->GetHandle(), nullptr);
	}
	for (auto &buffer_alloc : m_internal_buffers) {
		vmaBindBufferMemory2(m_p_render_graph->GetDevicePtr()->GetAllocatorHandle(),
		                     m_allocations[buffer_alloc.allocation_id].myvk_allocation->GetHandle(),
		                     buffer_alloc.memory_offset, buffer_alloc.myvk_buffer->GetHandle(), nullptr);
	}

	printf("\nAllocations: \n");
	for (const auto &allocation_info : m_allocations) {
		VmaAllocationInfo info;
		vmaGetAllocationInfo(m_p_render_graph->GetDevicePtr()->GetAllocatorHandle(),
		                     allocation_info.myvk_allocation->GetHandle(), &info);
		VkMemoryPropertyFlags flags;
		vmaGetAllocationMemoryProperties(m_p_render_graph->GetDevicePtr()->GetAllocatorHandle(),
		                                 allocation_info.myvk_allocation->GetHandle(), &flags);
		printf("allocation: size = %lu MB, memory_type = %u\n", info.size >> 20u, flags);
	}
	printf("\n");
}

void RenderGraphAllocator::reset_resource_arrays() {
	m_internal_images.clear();
	m_internal_image_views.clear();
	m_internal_buffers.clear();

	m_internal_images.resize(m_p_resolved->GetIntImageCount());
	m_internal_image_views.resize(m_p_resolved->GetIntImageViewCount());
	m_internal_buffers.resize(m_p_resolved->GetIntBufferCount());

	for (uint32_t buffer_id = 0; buffer_id < m_p_resolved->GetIntBufferCount(); ++buffer_id) {
		const auto &buffer_info = m_p_resolved->GetIntBufferInfo(buffer_id);
		auto &buffer_alloc = m_internal_buffers[buffer_id];
		buffer_alloc.internal_resource_id = m_p_resolved->GetIntResourceID(buffer_info.buffer);
		buffer_alloc.p_info = &buffer_info;
	}

	// Create Images
	for (uint32_t image_id = 0; image_id < m_p_resolved->GetIntImageCount(); ++image_id) {
		const auto &image_info = m_p_resolved->GetIntImageInfo(image_id);
		auto &image_alloc = m_internal_images[image_id];
		image_alloc.internal_resource_id = m_p_resolved->GetIntResourceID(image_info.image);
		image_alloc.p_info = &image_info;
	}
}

void RenderGraphAllocator::Allocate(const RenderGraphBase *p_render_graph, const RenderGraphResolver &resolved) {
	m_p_render_graph = p_render_graph;
	m_p_resolved = &resolved;

	reset_resource_arrays();
	update_image_info();
	create_vk_resources();
	create_and_bind_allocations();
	create_vk_image_views();

	printf("\nImages: \n");
	for (uint32_t i = 0; i < m_p_resolved->GetIntImageCount(); ++i) {
		const auto &image_info = m_p_resolved->GetIntImageInfo(i);
		const auto &image_alloc = m_internal_images[i];
		std::cout << image_info.image->GetKey().GetName() << ":" << image_info.image->GetKey().GetID()
		          << " mip_levels = " << image_alloc.p_size->GetMipLevels() << " usage = " << image_info.vk_image_usages
		          << " {size, alignment, flag} = {" << image_alloc.vk_memory_requirements.size << ", "
		          << image_alloc.vk_memory_requirements.alignment << ", "
		          << image_alloc.vk_memory_requirements.memoryTypeBits << "}"
		          << " transient = " << image_info.is_transient << " offset = " << image_alloc.memory_offset
		          << std::endl;
	}
	printf("\n");

	printf("\nBuffers: \n");
	for (uint32_t i = 0; i < m_p_resolved->GetIntBufferCount(); ++i) {
		const auto &buffer_info = m_p_resolved->GetIntBufferInfo(i);
		const auto &buffer_alloc = m_internal_buffers[i];
		std::cout << buffer_info.buffer->GetKey().GetName() << ":" << buffer_info.buffer->GetKey().GetID()
		          << " {size, alignment, flag} = {" << buffer_alloc.vk_memory_requirements.size << ", "
		          << buffer_alloc.vk_memory_requirements.alignment << ", "
		          << buffer_alloc.vk_memory_requirements.memoryTypeBits << "}"
		          << " offset = " << buffer_alloc.memory_offset << std::endl;
	}
	printf("\n");
}

} // namespace myvk_rg::_details_