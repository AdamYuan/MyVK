#pragma once
#ifndef MYVK_EXPORT_BUFFER_HPP
#define MYVK_EXPORT_BUFFER_HPP

#include "BufferBase.hpp"

namespace myvk {

class ExportBuffer final : public BufferBase {
public:
	struct Handle {
		VkBuffer buffer{VK_NULL_HANDLE};
		VkDeviceMemory device_memory{VK_NULL_HANDLE};
		VkExternalMemoryHandleTypeFlagBits ext_handle_type{};
		void *mem_handle{nullptr};
		bool IsValid() const { return buffer && device_memory && ext_handle_type && mem_handle; }
		explicit operator bool() const { return IsValid(); }
	};
	static Handle CreateHandle(const Ptr<Device> &device, VkDeviceSize size, VkBufferUsageFlags usage,
	                           VkMemoryPropertyFlags memory_properties,
	                           const std::vector<Ptr<Queue>> &access_queues = {});

private:
	Ptr<Device> m_device_ptr;
	VkDeviceMemory m_device_memory{VK_NULL_HANDLE};
	VkExternalMemoryHandleTypeFlagBits m_ext_handle_type{};
	void *m_mem_handle{nullptr};

public:
	~ExportBuffer() override;
	static const char *GetExternalMemoryExtensionName();
	static Ptr<ExportBuffer> Create(const Ptr<Device> &device, VkDeviceSize size, VkBufferUsageFlags usage,
	                                VkMemoryPropertyFlags memory_properties,
	                                const std::vector<Ptr<Queue>> &access_queues = {});
	inline void *GetMemoryHandle() const { return m_mem_handle; }
	inline VkDeviceMemory GetDeviceMemoryHandle() const { return m_device_memory; }
	inline VkExternalMemoryHandleTypeFlagBits GetMemoryHandleType() const { return m_ext_handle_type; }

	inline const Ptr<Device> &GetDevicePtr() const override { return m_device_ptr; }
};

} // namespace myvk

#endif
