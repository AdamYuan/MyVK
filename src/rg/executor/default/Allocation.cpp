//
// Created by adamyuan on 2/8/24.
//

#include "Allocation.hpp"
#include <algorithm>

#include "../VkHelper.hpp"
#include "Info.hpp"

namespace default_executor {

using Meta = ResourceMeta;

class RGImage final : public myvk::ImageBase {
private:
	myvk::Ptr<myvk::Device> m_device_ptr;

public:
	inline RGImage(const myvk::Ptr<myvk::Device> &device, const VkImageCreateInfo &create_info) : m_device_ptr{device} {
		vkCreateImage(GetDevicePtr()->GetHandle(), &create_info, nullptr, &m_image);
		m_extent = create_info.extent;
		m_mip_levels = create_info.mipLevels;
		m_array_layers = create_info.arrayLayers;
		m_format = create_info.format;
		m_type = create_info.imageType;
		m_usage = create_info.usage;
	}
	inline ~RGImage() final {
		if (m_image != VK_NULL_HANDLE)
			vkDestroyImage(GetDevicePtr()->GetHandle(), m_image, nullptr);
	};
	const myvk::Ptr<myvk::Device> &GetDevicePtr() const final { return m_device_ptr; }
};
class RGBuffer final : public myvk::BufferBase {
private:
	myvk::Ptr<myvk::Device> m_device_ptr;

public:
	inline RGBuffer(const myvk::Ptr<myvk::Device> &device, const VkBufferCreateInfo &create_info)
	    : m_device_ptr{device} {
		vkCreateBuffer(GetDevicePtr()->GetHandle(), &create_info, nullptr, &m_buffer);
		m_size = create_info.size;
	}
	inline ~RGBuffer() final {
		if (m_buffer != VK_NULL_HANDLE)
			vkDestroyBuffer(GetDevicePtr()->GetHandle(), m_buffer, nullptr);
	}
	const myvk::Ptr<myvk::Device> &GetDevicePtr() const final { return m_device_ptr; }
};

class RGMemoryAllocation final : public myvk::DeviceObjectBase {
private:
	myvk::Ptr<myvk::Device> m_device_ptr;
	VmaAllocation m_allocation{VK_NULL_HANDLE};
	VmaAllocationInfo m_info{};

public:
	inline RGMemoryAllocation(const myvk::Ptr<myvk::Device> &device, const VkMemoryRequirements &memory_requirements,
	                          const VmaAllocationCreateInfo &create_info)
	    : m_device_ptr{device} {
		vmaAllocateMemory(device->GetAllocatorHandle(), &memory_requirements, &create_info, &m_allocation, &m_info);
		if (create_info.flags & VMA_ALLOCATION_CREATE_MAPPED_BIT) {
			assert(m_info.pMappedData);
		}
	}
	inline ~RGMemoryAllocation() final {
		if (m_allocation != VK_NULL_HANDLE)
			vmaFreeMemory(GetDevicePtr()->GetAllocatorHandle(), m_allocation);
	}
	inline const VmaAllocationInfo &GetInfo() const { return m_info; }
	inline VmaAllocation GetHandle() const { return m_allocation; }
	const myvk::Ptr<myvk::Device> &GetDevicePtr() const final { return m_device_ptr; }
};

Allocation Allocation::Create(const myvk::Ptr<myvk::Device> &device_ptr, const Args &args) {
	Allocation alloc = {};
	alloc.m_device_ptr = device_ptr;

	alloc.create_vk_resources(args);
	alloc.create_vk_allocations(args);
	alloc.bind_vk_resources(args);
	alloc.create_vk_image_views(args);

	return alloc;
}

void Allocation::create_vk_resources(const Args &args) {
	const auto create_image = [&](const ImageBase *p_image) {
		auto &alloc = get_alloc_info(p_image);
		auto &meta = Meta::GetMeta(p_image);

		VkImageCreateInfo create_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
		create_info.usage = meta.image.vk_usages;
		// if (image_info.is_transient)
		//     create_info.usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
		create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		create_info.format = meta.image.vk_format;
		create_info.samples = VK_SAMPLE_COUNT_1_BIT;
		create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
		create_info.imageType = meta.image.vk_type;
		{ // Set Size Info
			VkExtent3D &extent = create_info.extent;
			extent = {1, 1, 1};

			const SubImageSize &size = meta.image.size;
			switch (create_info.imageType) {
			case VK_IMAGE_TYPE_1D: {
				extent.width = size.GetExtent().width;
				create_info.mipLevels = size.GetMipLevels();
				create_info.arrayLayers = size.GetArrayLayers();
			} break;
			case VK_IMAGE_TYPE_2D: {
				extent.width = size.GetExtent().width;
				extent.height = size.GetExtent().height;
				create_info.mipLevels = size.GetMipLevels();
				create_info.arrayLayers = size.GetArrayLayers();
			} break;
			case VK_IMAGE_TYPE_3D: {
				extent.width = size.GetExtent().width;
				extent.height = size.GetExtent().height;
				extent.depth = std::max(size.GetExtent().depth, size.GetArrayLayers());
				create_info.mipLevels = size.GetMipLevels();
				create_info.arrayLayers = 1;
			} break;
			default:;
			}
		}

		alloc.image.myvk_images[0] = std::make_shared<RGImage>(m_device_ptr, create_info);
		vkGetImageMemoryRequirements(m_device_ptr->GetHandle(), alloc.image.myvk_images[0]->GetHandle(),
		                             &alloc.vk_mem_reqs);

		alloc.image.myvk_images[1] =
		    meta.double_buffer ? std::make_shared<RGImage>(m_device_ptr, create_info) : alloc.image.myvk_images[0];
	};
	const auto create_buffer = [&](const BufferBase *p_buffer) {
		auto &alloc = get_alloc_info(p_buffer);
		auto &meta = Meta::GetMeta(p_buffer);

		VkBufferCreateInfo create_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
		create_info.usage = meta.buffer.vk_usages;
		create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		create_info.size = meta.buffer.size;

		alloc.buffer.myvk_buffers[0] = std::make_shared<RGBuffer>(m_device_ptr, create_info);
		vkGetBufferMemoryRequirements(m_device_ptr->GetHandle(), alloc.buffer.myvk_buffers[0]->GetHandle(),
		                              &alloc.vk_mem_reqs);

		alloc.buffer.myvk_buffers[1] =
		    meta.double_buffer ? std::make_shared<RGBuffer>(m_device_ptr, create_info) : alloc.buffer.myvk_buffers[0];
	};

	for (const ResourceBase *p_resource : args.resource_meta.GetAllocIDResources())
		p_resource->Visit(overloaded(create_image, create_buffer));
}

inline static constexpr VkDeviceSize DivCeil(VkDeviceSize l, VkDeviceSize r) { return (l / r) + (l % r ? 1 : 0); }

std::tuple<VkDeviceSize, uint32_t> Allocation::fetch_memory_requirements(std::ranges::input_range auto &&resources) {
	VkDeviceSize alignment = 1;
	uint32_t memory_type_bits = -1;
	for (const ResourceBase *p_resource : resources) {
		const auto &mem_reqs = get_alloc_info(p_resource).vk_mem_reqs;
		alignment = std::max(alignment, mem_reqs.alignment);
		memory_type_bits &= mem_reqs.memoryTypeBits;
	}
	return {alignment, memory_type_bits};
}
void Allocation::alloc_naive(std::ranges::input_range auto &&resources, const VmaAllocationCreateInfo &create_info) {
	auto [alignment, memory_type_bits] = fetch_memory_requirements(resources);
	VkDeviceSize mem_total = 0;
	for (const ResourceBase *p_resource : resources) {
		auto &alloc = get_alloc_info(p_resource);

		VkDeviceSize mem_size = DivCeil(alloc.vk_mem_reqs.size, alignment);

		alloc.mem_offsets[0] = mem_total * alignment;
		mem_total += mem_size;

		if (Meta::GetMeta(p_resource).double_buffer) {
			alloc.mem_offsets[1] = mem_total * alignment;
			mem_total += mem_size;
		}
	}
	if (mem_total == 0)
		return;

	VkMemoryRequirements mem_reqs = {
	    .size = mem_total * alignment,
	    .alignment = alignment,
	    .memoryTypeBits = memory_type_bits,
	};
	auto mem_alloc = myvk::MakePtr<RGMemoryAllocation>(m_device_ptr, mem_reqs, create_info);
	for (const ResourceBase *p_resource : resources)
		get_alloc_info(p_resource).myvk_mem_alloc = mem_alloc;
}

namespace alloc_optimal {
// An AABB indicates a placed resource
struct MemBlock {
	VkDeviceSize mem_begin, mem_end;
	const ResourceBase *p_resource;
};
struct MemEvent {
	VkDeviceSize mem_pos;
	uint32_t cnt;
	inline bool operator<(const MemEvent &r) const { return mem_pos < r.mem_pos; }
};
} // namespace alloc_optimal

void Allocation::alloc_optimal(const Args &args, std::ranges::input_range auto &&resources,
                               const VmaAllocationCreateInfo &create_info) {
	auto [alignment, memory_type_bits] = fetch_memory_requirements(resources);

	std::ranges::sort(resources, [](const ResourceBase *p_l, const ResourceBase *p_r) -> bool {
		const auto &reqs_l = get_alloc_info(p_l).vk_mem_reqs, reqs_r = get_alloc_info(p_r).vk_mem_reqs;
		return reqs_l.size > reqs_r.size ||
		       (reqs_l.size == reqs_r.size && Dependency::GetResourceAccessPassBitset(p_l).GetFirstBit() <
		                                          Dependency::GetResourceAccessPassBitset(p_r).GetFirstBit());
	});

	using alloc_optimal::MemBlock;
	using alloc_optimal::MemEvent;

	std::vector<MemBlock> blocks;
	std::vector<MemEvent> events;
	blocks.reserve(resources.size());
	events.reserve(resources.size() << 1u);

	VkDeviceSize mem_total = 0;

	for (const ResourceBase *p_resource : resources) {
		// Find an empty position to place
		events.clear();
		for (const auto &block : blocks)
			if (args.dependency.IsResourceConflicted(p_resource, block.p_resource)) {
				events.push_back({block.mem_begin, 1});
				events.push_back({block.mem_end, (uint32_t)-1});
			}
		std::sort(events.begin(), events.end());

		VkDeviceSize required_mem_size = DivCeil(get_alloc_info(p_resource).vk_mem_reqs.size, alignment);

		VkDeviceSize optimal_mem_pos = 0, optimal_mem_size = std::numeric_limits<VkDeviceSize>::max();
		if (!events.empty()) {
			if (events.front().mem_pos >= required_mem_size)
				optimal_mem_size = events.front().mem_pos;
			else
				optimal_mem_pos = events.back().mem_pos;

			for (std::size_t i = 1; i < events.size(); ++i) {
				events[i].cnt += events[i - 1].cnt;
				if (events[i - 1].cnt == 0 && events[i].cnt == 1) {
					VkDeviceSize cur_mem_pos = events[i - 1].mem_pos,
					             cur_mem_size = events[i].mem_pos - events[i - 1].mem_pos;
					if (required_mem_size <= cur_mem_size && cur_mem_size < optimal_mem_size) {
						optimal_mem_size = cur_mem_size;
						optimal_mem_pos = cur_mem_pos;
					}
				}
			}
		}

		get_alloc_info(p_resource).mem_offsets[0] = optimal_mem_pos * alignment;
		mem_total = std::max(mem_total, optimal_mem_pos + required_mem_size);

		blocks.push_back({optimal_mem_pos, optimal_mem_pos + required_mem_size, p_resource});
	}

	// Append alias relationships
	for (const ResourceBase *p_l : resources) {
		const auto &alloc_l = get_alloc_info(p_l);
		for (const ResourceBase *p_r : resources) {
			const auto &alloc_r = get_alloc_info(p_r);
			// Check Overlapping
			if (alloc_l.mem_offsets[0] + alloc_l.vk_mem_reqs.size > alloc_r.mem_offsets[0] &&
			    alloc_r.mem_offsets[0] + alloc_r.vk_mem_reqs.size > alloc_l.mem_offsets[0])
				m_resource_alias_relation.Add(Meta::GetResourceAllocID(p_l), Meta::GetResourceAllocID(p_r));
		}
	}

	if (mem_total == 0)
		return;

	VkMemoryRequirements mem_reqs = {
	    .size = mem_total * alignment,
	    .alignment = alignment,
	    .memoryTypeBits = memory_type_bits,
	};
	auto mem_alloc = myvk::MakePtr<RGMemoryAllocation>(m_device_ptr, mem_reqs, create_info);
	for (const ResourceBase *p_resource : resources)
		get_alloc_info(p_resource).myvk_mem_alloc = mem_alloc;
}

void Allocation::create_vk_allocations(const Args &args) {
	m_resource_alias_relation.Reset(args.resource_meta.GetAllocResourceCount(),
	                                args.resource_meta.GetAllocResourceCount());

	std::vector<const ResourceBase *> local_resources, lf_resources, random_mapped_resources,
	    seq_write_mapped_resources;

	for (const ResourceBase *p_resource : args.resource_meta.GetAllocIDResources())
		p_resource->Visit(overloaded(
		    [&](const LocalInternalImage auto *p_image) {
			    if (Dependency::GetLFResource(p_image))
				    lf_resources.push_back(p_image);
			    else
				    local_resources.push_back(p_image);
		    },
		    [&](const LocalInternalBuffer auto *p_buffer) {
			    switch (p_buffer->GetMapType()) {
			    case myvk_rg::BufferMapType::kNone: {
				    if (Dependency::GetLFResource(p_buffer))
					    lf_resources.push_back(p_buffer);
				    else
					    local_resources.push_back(p_buffer);
			    } break;
			    case myvk_rg::BufferMapType::kRandom:
				    random_mapped_resources.push_back(p_buffer);
				    break;
			    case myvk_rg::BufferMapType::kSeqWrite:
				    seq_write_mapped_resources.push_back(p_buffer);
				    break;
			    }
		    },
		    [](auto &&) {}));

	alloc_optimal(args, local_resources,
	              VmaAllocationCreateInfo{
	                  .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT | VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT,
	                  .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
	              });
	alloc_naive(lf_resources, VmaAllocationCreateInfo{
	                              .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
	                              .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
	                          });
	alloc_naive(random_mapped_resources,
	            VmaAllocationCreateInfo{
	                .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT |
	                         VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
	                .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
	            });
	alloc_naive(seq_write_mapped_resources,
	            VmaAllocationCreateInfo{
	                .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT |
	                         VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
	                .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
	            });
}

void Allocation::bind_vk_resources(const Args &args) {
	for (const ResourceBase *p_resource : args.resource_meta.GetAllocIDResources()) {
		auto &alloc = get_alloc_info(p_resource);
		auto &meta = Meta::GetMeta(p_resource);

		auto vma_allocation = alloc.myvk_mem_alloc->GetHandle();

		p_resource->Visit(overloaded(
		    [&](const ImageBase *p_image) {
			    vmaBindImageMemory2(m_device_ptr->GetAllocatorHandle(), vma_allocation, alloc.mem_offsets[0],
			                        alloc.image.myvk_images[0]->GetHandle(), nullptr);
			    if (meta.double_buffer)
				    vmaBindImageMemory2(m_device_ptr->GetAllocatorHandle(), vma_allocation, alloc.mem_offsets[1],
				                        alloc.image.myvk_images[1]->GetHandle(), nullptr);
		    },
		    [&](const BufferBase *p_buffer) {
			    auto *p_mapped = (uint8_t *)alloc.myvk_mem_alloc->GetInfo().pMappedData;

			    vmaBindBufferMemory2(m_device_ptr->GetAllocatorHandle(), vma_allocation, alloc.mem_offsets[0],
			                         alloc.buffer.myvk_buffers[0]->GetHandle(), nullptr);
			    alloc.buffer.mapped_ptrs[0] = p_mapped + alloc.mem_offsets[0];
			    if (meta.double_buffer) {
				    vmaBindBufferMemory2(m_device_ptr->GetAllocatorHandle(), vma_allocation, alloc.mem_offsets[1],
				                         alloc.buffer.myvk_buffers[1]->GetHandle(), nullptr);
				    alloc.buffer.mapped_ptrs[1] = p_mapped + alloc.mem_offsets[1];
			    } else
				    alloc.buffer.mapped_ptrs[1] = alloc.buffer.mapped_ptrs[0];
		    }));
	}
}

void Allocation::create_vk_image_views(const Args &args) {
	const auto create_image_view = [&](const LocalInternalImage auto *p_image) {
		auto &image_alloc = get_alloc_info(p_image).image;
		auto &image_meta = Meta::GetMeta(p_image).image;
		auto &root_image_alloc = get_alloc_info(Meta::GetAllocResource(p_image)).image;

		VkImageViewCreateInfo create_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
		create_info.format = image_meta.vk_format;
		create_info.viewType = p_image->GetViewType();
		create_info.subresourceRange.baseArrayLayer = image_meta.base_layer;
		create_info.subresourceRange.layerCount = image_meta.size.GetArrayLayers();
		create_info.subresourceRange.baseMipLevel = image_meta.size.GetBaseMipLevel();
		create_info.subresourceRange.levelCount = image_meta.size.GetMipLevels();
		create_info.subresourceRange.aspectMask = VkImageAspectFlagsFromVkFormat(image_meta.vk_format);

		image_alloc.myvk_image_views[0] = myvk::ImageView::Create(root_image_alloc.myvk_images[0], create_info);
		image_alloc.myvk_image_views[1] = root_image_alloc.myvk_images[1] != root_image_alloc.myvk_images[0]
		                                      ? myvk::ImageView::Create(root_image_alloc.myvk_images[1], create_info)
		                                      : image_alloc.myvk_image_views[0];
	};
	for (const ResourceBase *p_resource : args.resource_meta.GetViewIDResources())
		p_resource->Visit(overloaded(create_image_view, [](auto &&) {}));
}

} // namespace default_executor
