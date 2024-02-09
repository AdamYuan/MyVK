//
// Created by adamyuan on 2/5/24.
//

#pragma once
#ifndef MYVK_INFO_HPP
#define MYVK_INFO_HPP

#include "../Bitset.hpp"
#include <array>
#include <myvk_rg/interface/RenderGraph.hpp>

namespace default_executor {

using namespace myvk_rg::interface;
using namespace myvk_rg::executor;

struct PassInfo {
	struct {
		std::size_t topo_id{};
		friend class Dependency;
	} dependency;
};

struct ResourceInfo {
	// Dependency
	struct {
		friend class Dependency;

	private:
		std::size_t phys_id{};
		const ResourceBase *p_root_resource{}, *p_lf_resource{};
		Bitset access_passes;
	} dependency;

	// Allocation
	struct {
		friend class Allocation;

	private:
		std::size_t alloc_id{}, view_id{};
		const ResourceBase *p_alloc_resource{}, *p_view_resource{};
		struct {
			SubImageSize size{};
			uint32_t base_layer{};
			VkImageViewType vk_view_type{};
			VkImageType vk_type{};
			VkFormat vk_format{};
			VkImageUsageFlags vk_usages{};
			std::array<myvk::Ptr<myvk::ImageBase>, 2> myvk_images{};
			std::array<myvk::Ptr<myvk::ImageView>, 2> myvk_image_views{};
		} image{};
		struct {
			VkDeviceSize size{};
			VkBufferUsageFlags vk_usages{};
			std::array<myvk::Ptr<myvk::BufferBase>, 2> myvk_buffers{};
		} buffer{};
		bool double_buffer{};
		VkMemoryRequirements vk_mem_reqs;
	} allocation;
};

inline PassInfo &GetPassInfo(const PassBase *p_pass) { return *p_pass->__GetPExecutorInfo<PassInfo>(); }
inline ResourceInfo &GetResourceInfo(const ResourceBase *p_resource) {
	return *p_resource->__GetPExecutorInfo<ResourceInfo>();
}

} // namespace default_executor

#endif // MYVK_INFO_HPP
