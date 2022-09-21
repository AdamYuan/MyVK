#include "myvk/PhysicalDevice.hpp"
#include "myvk/Surface.hpp"

namespace myvk {
void PhysicalDevice::initialize(const Ptr<Instance> &instance, VkPhysicalDevice physical_device) {
	m_instance_ptr = instance;
	m_physical_device = physical_device;
	vkGetPhysicalDeviceProperties(physical_device, &m_properties);
	vkGetPhysicalDeviceMemoryProperties(physical_device, &m_memory_properties);
	vkGetPhysicalDeviceFeatures(physical_device, &m_features);
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

#ifdef MYVK_ENABLE_GLFW
bool PhysicalDevice::GetSurfaceSupport(uint32_t queue_family_index, const Ptr<Surface> &surface) const {
	VkBool32 support;
	if (vkGetPhysicalDeviceSurfaceSupportKHR(m_physical_device, queue_family_index, surface->GetHandle(), &support) !=
	    VK_SUCCESS)
		return false;
	return support;
}
#endif
} // namespace myvk
