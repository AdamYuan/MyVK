#ifndef MYVK_DEVICE_HPP
#define MYVK_DEVICE_HPP

#include "DeviceCreateInfo.hpp"
#include "PhysicalDevice.hpp"
#include "Ptr.hpp"
#include "Surface.hpp"
#include "vk_mem_alloc.h"
#include "volk.h"
#include <memory>
#include <vector>

namespace myvk {
class Device {
private:
	Ptr<PhysicalDevice> m_physical_device_ptr;

	VkDevice m_device{VK_NULL_HANDLE};
	VkPipelineCache m_pipeline_cache{VK_NULL_HANDLE};
	VmaAllocator m_allocator{VK_NULL_HANDLE};

	VkResult create_allocator();

	VkResult create_device(const std::vector<VkDeviceQueueCreateInfo> &queue_create_infos,
	                       const std::vector<const char *> &extensions, void *p_next);

	VkResult create_pipeline_cache();

public:
	// TODO: enable custom device features
	static Ptr<Device> Create(const DeviceCreateInfo &device_create_info, void *p_next = nullptr);

	VmaAllocator GetAllocatorHandle() const { return m_allocator; }

	VkPipelineCache GetPipelineCacheHandle() const { return m_pipeline_cache; }

	const Ptr<PhysicalDevice> &GetPhysicalDevicePtr() const { return m_physical_device_ptr; }

	VkDevice GetHandle() const { return m_device; }

	VkResult WaitIdle() const;

	~Device();
};
} // namespace myvk

#endif
