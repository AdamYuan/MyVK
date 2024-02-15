//
// Created by adamyuan on 2/12/24.
//

#pragma once
#ifndef MYVK_RG_DEF_EXE_VKCOMMAND_HPP
#define MYVK_RG_DEF_EXE_VKCOMMAND_HPP

#include "Schedule.hpp"
#include "VkAllocation.hpp"

namespace default_executor {

class VkCommand {
public:
	struct BarrierCmd {
		const ResourceBase *p_resource;
		VkPipelineStageFlags2 src_stage_mask;
		VkPipelineStageFlags2 dst_stage_mask;
		VkAccessFlags2 src_access_mask;
		VkAccessFlags2 dst_access_mask;
		VkImageLayout old_layout;
		VkImageLayout new_layout;
	};
	struct PassCmd {
		std::span<const PassBase *const> subpasses; // pointed to subpasses in Schedule::PassGroup
		std::vector<BarrierCmd> prior_barriers;

		myvk::Ptr<myvk::RenderPass> myvk_render_pass;
		myvk::Ptr<myvk::ImagelessFramebuffer> myvk_framebuffer;
		std::vector<const ImageBase *> attachments;
	};

private:
	struct Args {
		const RenderGraphBase &render_graph;
		const Collection &collection;
		const Dependency &dependency;
		const Metadata &metadata;
		const Schedule &schedule;
		const VkAllocation &vk_allocation;
	};
	class Builder;

	std::vector<PassCmd> m_pass_commands;
	std::vector<BarrierCmd> m_post_barriers;

public:
	static VkCommand Create(const myvk::Ptr<myvk::Device> &device_ptr, const Args &args);
	inline const auto &GetPassCommands() const { return m_pass_commands; }
	inline const auto &GetPostBarriers() const { return m_post_barriers; }
	static void CreatePipeline(const PassBase *p_pass) {
		if (GetPassInfo(p_pass).vk_command.update_pipeline) {
			GetPassInfo(p_pass).vk_command.update_pipeline = false;
			((PassBase *)p_pass)
			    ->Visit(overloaded([](PassWithPipeline auto *p_pipeline_pass) { p_pipeline_pass->CreatePipeline(); },
			                       [](auto &&) {}));
		}
	}
	static void UpdatePipeline(const PassBase *p_pass) { GetPassInfo(p_pass).vk_command.update_pipeline = true; }
};

} // namespace default_executor

#endif
