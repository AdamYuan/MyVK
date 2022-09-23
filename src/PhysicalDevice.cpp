#include "myvk/PhysicalDevice.hpp"

namespace myvk {
void PhysicalDevice::initialize(const Ptr<Instance> &instance, VkPhysicalDevice physical_device) {
	m_instance_ptr = instance;
	m_physical_device = physical_device;
	vkGetPhysicalDeviceMemoryProperties(physical_device, &m_memory_properties);
	{
		VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &m_vk11_features};
		vkGetPhysicalDeviceFeatures2(physical_device, &features2);
		m_features = features2.features;
	}
	{
		VkPhysicalDeviceProperties2 properties2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &m_vk11_properties};
		vkGetPhysicalDeviceProperties2(physical_device, &properties2);
		m_properties = properties2.properties;
	}
	{
		uint32_t count;
		vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
		m_queue_family_properties.resize(count);
		vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, m_queue_family_properties.data());
	}
}

std::vector<Ptr<PhysicalDevice>> PhysicalDevice::Fetch(const Ptr<Instance> &instance) {
	uint32_t device_count = 0;
	vkEnumeratePhysicalDevices(instance->GetHandle(), &device_count, nullptr);
	std::vector<VkPhysicalDevice> devices(device_count);
	vkEnumeratePhysicalDevices(instance->GetHandle(), &device_count, devices.data());

	std::vector<Ptr<PhysicalDevice>> ret;
	ret.resize(device_count);
	for (uint32_t i = 0; i < device_count; ++i) {
		auto ptr = std::make_shared<PhysicalDevice>();
		ptr->initialize(instance, devices[i]);
		ret[i] = ptr;
	}
	return ret;
}
} // namespace myvk
