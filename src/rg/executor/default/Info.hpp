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

struct InputInfo {
	// Dependency
	struct {
		friend class Dependency;

		const PassBase *p_pass{};
		const ResourceBase *p_resource{};
	} dependency{};
};

struct PassInfo {
	// Dependency
	struct {
		friend class Dependency;

	private:
		std::size_t topo_id{};
		std::vector<const InputBase *> inputs;
	} dependency{};

	// Schedule
	struct {
		friend class Schedule;

	private:
		RenderPassArea render_area;
		std::size_t group_id{}, subpass_id{};
	} schedule{};
};

class RGMemoryAllocation;
struct ResourceInfo {
	// Dependency
	struct {
		friend class Dependency;

	private:
		std::size_t phys_id{};
		const ResourceBase *p_root_resource{}, *p_lf_resource{};
		Bitset access_passes;
	} dependency{};

	// Meta
	struct {
		std::size_t alloc_id{}, view_id{};
		const ResourceBase *p_alloc_resource{}, *p_view_resource{};
		struct {
			SubImageSize size{};
			uint32_t base_layer{};
			VkImageType vk_type{};
			VkFormat vk_format{};
			VkImageUsageFlags vk_usages{};
		} image{};
		struct {
			VkDeviceSize size{};
			VkBufferUsageFlags vk_usages{};
		} buffer{};
		bool double_buffer{};
	} meta{};

	// Allocation
	struct {
		friend class Allocation;

	private:
		struct {
			std::array<myvk::Ptr<myvk::ImageBase>, 2> myvk_images{};
			std::array<myvk::Ptr<myvk::ImageView>, 2> myvk_image_views{};
		} image{};
		struct {
			std::array<myvk::Ptr<myvk::BufferBase>, 2> myvk_buffers{};
			std::array<void *, 2> mapped_ptrs{};
		} buffer{};
		VkMemoryRequirements vk_mem_reqs{};
		myvk::Ptr<RGMemoryAllocation> myvk_mem_alloc{};
		std::array<VkDeviceSize, 2> mem_offsets{};
	} allocation{};
};

inline PassInfo &GetPassInfo(const PassBase *p_pass) { return *p_pass->__GetPExecutorInfo<PassInfo>(); }
inline InputInfo &GetInputInfo(const InputBase *p_input) { return *p_input->__GetPExecutorInfo<InputInfo>(); }
inline ResourceInfo &GetResourceInfo(const ResourceBase *p_resource) {
	return *p_resource->__GetPExecutorInfo<ResourceInfo>();
}

} // namespace default_executor

#endif // MYVK_INFO_HPP
