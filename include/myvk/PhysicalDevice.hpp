#ifndef MYVK_PHYSICAL_DEVICE_HPP
#define MYVK_PHYSICAL_DEVICE_HPP

#include "Instance.hpp"

#include "volk.h"
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

namespace myvk {
class Surface;
class PhysicalDevice {
private:
	Ptr<Instance> m_instance_ptr;

	VkPhysicalDevice m_physical_device{VK_NULL_HANDLE};

	VkPhysicalDeviceMemoryProperties m_memory_properties;
	VkPhysicalDeviceFeatures m_features;
	VkPhysicalDeviceVulkan11Features m_vk11_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
	                                                 &m_vk12_features};
	VkPhysicalDeviceVulkan12Features m_vk12_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
	                                                 &m_vk13_features};
	VkPhysicalDeviceVulkan13Features m_vk13_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
	VkPhysicalDeviceProperties m_properties;
	VkPhysicalDeviceVulkan11Properties m_vk11_properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES,
	                                                     &m_vk12_properties};
	VkPhysicalDeviceVulkan12Properties m_vk12_properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES,
	                                                     &m_vk13_properties};
	VkPhysicalDeviceVulkan13Properties m_vk13_properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES};
	std::vector<VkQueueFamilyProperties> m_queue_family_properties;

	void initialize(const Ptr<Instance> &instance, VkPhysicalDevice physical_device);

public:
	static std::vector<Ptr<PhysicalDevice>> Fetch(const Ptr<Instance> &instance);

	const Ptr<Instance> &GetInstancePtr() const { return m_instance_ptr; }
	VkPhysicalDevice GetHandle() const { return m_physical_device; }
	const VkPhysicalDeviceMemoryProperties &GetMemoryProperties() const { return m_memory_properties; }
	const VkPhysicalDeviceProperties &GetProperties() const { return m_properties; }
	const VkPhysicalDeviceVulkan11Properties &GetVk11Properties() const { return m_vk11_properties; }
	const VkPhysicalDeviceVulkan12Properties &GetVk12Properties() const { return m_vk12_properties; }
	const VkPhysicalDeviceVulkan13Properties &GetVk13Properties() const { return m_vk13_properties; }
	const VkPhysicalDeviceFeatures &GetFeatures() const { return m_features; }
	const VkPhysicalDeviceVulkan11Features &GetVk11Features() const { return m_vk11_features; }
	const VkPhysicalDeviceVulkan12Features &GetVk12Features() const { return m_vk12_features; }
	const VkPhysicalDeviceVulkan13Features &GetVk13Features() const { return m_vk13_features; }
	const std::vector<VkQueueFamilyProperties> &GetQueueFamilyProperties() const { return m_queue_family_properties; }
	bool GetSurfaceSupport(uint32_t queue_family_index, const Ptr<Surface> &surface) const;
};
} // namespace myvk

#endif
