#include "myvk/Instance.hpp"

namespace myvk {
Ptr<Instance> Instance::Create(const VkInstanceCreateInfo &create_info) {
	if (vkCreateInstance == nullptr) {
		if (volkInitialize() != VK_SUCCESS)
			return nullptr;
	}
	auto ret = std::make_shared<Instance>();
	if (vkCreateInstance(&create_info, nullptr, &ret->m_instance) != VK_SUCCESS)
		return nullptr;
	volkLoadInstanceOnly(ret->m_instance);
	return ret;
}

Instance::~Instance() {
	if (m_instance) {
		if (m_debug_messenger)
			vkDestroyDebugUtilsMessengerEXT(m_instance, m_debug_messenger, nullptr);
		vkDestroyInstance(m_instance, nullptr);
	}
}
} // namespace myvk
