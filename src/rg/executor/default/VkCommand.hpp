//
// Created by adamyuan on 2/12/24.
//

#pragma once
#ifndef MYVK_RG_DEF_EXE_VKCOMMAND_HPP
#define MYVK_RG_DEF_EXE_VKCOMMAND_HPP

#include "VkAllocation.hpp"

namespace default_executor {

class VkCommand {
private:
	struct Args {
		const RenderGraphBase &render_graph;
		const Collection &collection;
		const Dependency &dependency;
		const Metadata &metadata;
		const VkAllocation &vk_allocation;
	};

	struct MemoryBarrier {
		const ResourceBase *p_resource;
		VkPipelineStageFlags2 src_stage_mask;
		VkAccessFlags2 src_access_mask;
		VkPipelineStageFlags2 dst_stage_mask;
		VkAccessFlags2 dst_access_mask;
		VkImageLayout old_layout;
		VkImageLayout new_layout;
		inline bool IsValid() const {
			return (src_stage_mask | src_access_mask) && (dst_stage_mask | dst_access_mask) || old_layout != new_layout;
		}
	};

	struct AttachmentInfo {
		const ImageBase *p_image{};
		std::vector<const InputBase *> references;
	};
	struct RenderPassInfo {
		myvk::Ptr<myvk::RenderPass> myvk_render_pass;
		myvk::Ptr<myvk::ImagelessFramebuffer> myvk_framebuffer;
		std::vector<AttachmentInfo> attachments;
	};
	struct BarrierInfo {
		std::vector<BufferMemoryBarrier> buffer_barriers;
		std::vector<ImageMemoryBarrier> image_barriers;

		inline bool empty() const { return buffer_barriers.empty() && image_barriers.empty(); }
		inline void clear() {
			buffer_barriers.clear();
			image_barriers.clear();
		}
	};
	struct PassExecutor {
		const RenderGraphScheduler::PassInfo *p_info{};
		BarrierInfo prior_barrier_info;
		RenderPassInfo render_pass_info;
	};
	std::vector<PassExecutor> m_pass_executors;
	BarrierInfo m_post_barrier_info;

public:
	static VkCommand Create(const myvk::Ptr<myvk::Device> &device_ptr, const Args &args);
};

} // namespace default_executor

#endif
