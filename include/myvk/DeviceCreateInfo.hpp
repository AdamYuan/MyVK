#ifndef MYVK_QUEUE_QUERY_HPP
#define MYVK_QUEUE_QUERY_HPP

#include "PhysicalDevice.hpp"
#include "Ptr.hpp"
#include "QueueSelector.hpp"
#include "volk.h"
#include <map>
#include <memory>
#include <vector>

namespace myvk {
class Queue;
class PresentQueue;
class Device;
class Surface;

class DeviceCreateInfo {
private:
	Ptr<PhysicalDevice> m_physical_device_ptr;
	std::vector<QueueSelection> m_queue_selections;
	std::vector<PresentQueueSelection> m_present_queue_selections;
	std::map<uint32_t,
	         std::vector<std::pair<std::vector<const QueueSelection *>, std::vector<const PresentQueueSelection *>>>>
	    m_queue_creations;
	std::vector<const char *> m_extensions;
	bool m_extension_support{true}, m_queue_support{true};

	bool m_use_allocator{true}, m_use_pipeline_cache{true};

	void enumerate_device_queue_create_infos(std::vector<VkDeviceQueueCreateInfo> *out_create_infos,
	                                         std::vector<float> *out_priorities) const;

	void fetch_queues(const Ptr<Device> &device) const;

	void generate_queue_creations();

	friend class Device;

public:
	inline DeviceCreateInfo() = default;
	inline DeviceCreateInfo(const Ptr<PhysicalDevice> &physical_device, const QueueSelectorFunc &queue_selector_func,
	                        const std::vector<const char *> &extensions, bool use_allocator = true,
	                        bool use_pipeline_cache = true) {
		Initialize(physical_device, queue_selector_func, extensions, use_allocator, use_pipeline_cache);
	}
	void Initialize(const Ptr<PhysicalDevice> &physical_device, const QueueSelectorFunc &queue_selector_func,
	                const std::vector<const char *> &extensions, bool use_allocator = true,
	                bool use_pipeline_cache = true);

	bool QueueSupport() const { return m_queue_support; }
	bool ExtensionSupport() const { return m_extension_support; }
};
} // namespace myvk

#endif
