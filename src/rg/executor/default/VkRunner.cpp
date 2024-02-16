//
// Created by adamyuan on 2/16/24.
//

#include "VkRunner.hpp"

namespace myvk_rg_executor {

void VkRunner::Run(const myvk::Ptr<myvk::CommandBuffer> &command_buffer, const VkCommand &vk_command,
                   const VkDescriptor &vk_descriptor) {
	const auto cmd_pipeline_barriers = [&](std::span<const VkCommand::BarrierCmd> barrier_cmds) {
		if (barrier_cmds.empty())
			return;

		std::vector<VkBufferMemoryBarrier2> buffer_barriers;
		std::vector<VkImageMemoryBarrier2> image_barriers;

		for (const auto &cmd : barrier_cmds) {
			cmd.p_resource->Visit(overloaded(
			    [&](const ImageResource auto *p_image) {
				    const auto &myvk_view = p_image->GetVkImageView();
				    image_barriers.push_back({.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
				                              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				                              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				                              .image = myvk_view->GetImagePtr()->GetHandle(),
				                              .subresourceRange = myvk_view->GetSubresourceRange()});
				    CopyVkBarrier(cmd, &image_barriers.back());
			    },
			    [&](const BufferResource auto *p_buffer) {
				    const auto &myvk_buffer = p_buffer->GetVkBuffer();
				    buffer_barriers.push_back({.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
				                               .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				                               .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				                               .buffer = myvk_buffer->GetHandle(),
				                               .offset = 0u,
				                               .size = myvk_buffer->GetSize()});
				    CopyVkBarrier(cmd, &buffer_barriers.back());
			    }));
		}

		VkDependencyInfo dep_info = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		                             .bufferMemoryBarrierCount = (uint32_t)buffer_barriers.size(),
		                             .pBufferMemoryBarriers = buffer_barriers.data(),
		                             .imageMemoryBarrierCount = (uint32_t)image_barriers.size(),
		                             .pImageMemoryBarriers = image_barriers.data()};
		vkCmdPipelineBarrier2(command_buffer->GetHandle(), &dep_info);
	};

	const auto run_pass = [&](const PassBase *p_pass) {
		VkCommand::CreatePipeline(p_pass);
		p_pass->CmdExecute(command_buffer);
	};

	for (const auto &pass_cmd : vk_command.GetPassCommands()) {
		cmd_pipeline_barriers(pass_cmd.prior_barriers);

		if (pass_cmd.myvk_render_pass) {
			const auto &attachments = pass_cmd.attachments;
			// Fetch Attachment Clear Values and Attachment Image Views
			std::vector<VkClearValue> clear_values;
			std::vector<VkImageView> attachment_vk_views;
			clear_values.reserve(attachments.size());
			attachment_vk_views.reserve(attachments.size());

			for (const auto &att : attachments) {
				att->Visit(overloaded(
				    [&](const AttachmentImage auto *p_att_image) {
					    clear_values.push_back(p_att_image->GetClearValue());
				    },
				    [&](const auto *p_image) { clear_values.push_back({}); }));
				attachment_vk_views.push_back(att->GetVkImageView()->GetHandle());
			}

			VkRenderPassAttachmentBeginInfo attachment_begin_info = {
			    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO,
			    .attachmentCount = (uint32_t)attachment_vk_views.size(),
			    .pAttachments = attachment_vk_views.data()};

			VkRenderPassBeginInfo render_begin_info = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
			render_begin_info.renderPass = pass_cmd.myvk_render_pass->GetHandle();
			render_begin_info.framebuffer = pass_cmd.myvk_framebuffer->GetHandle();
			render_begin_info.renderArea.offset = {0u, 0u};
			render_begin_info.renderArea.extent = Metadata::GetPassRenderArea(pass_cmd.subpasses[0]).extent;
			render_begin_info.clearValueCount = clear_values.size();
			render_begin_info.pClearValues = clear_values.data();
			render_begin_info.pNext = &attachment_begin_info;

			vkCmdBeginRenderPass(command_buffer->GetHandle(), &render_begin_info, VK_SUBPASS_CONTENTS_INLINE);

			run_pass(pass_cmd.subpasses.front());
			for (std::size_t i = 1; i < pass_cmd.subpasses.size(); ++i) {
				vkCmdNextSubpass(command_buffer->GetHandle(), VK_SUBPASS_CONTENTS_INLINE);
				run_pass(pass_cmd.subpasses[i]);
			}

			vkCmdEndRenderPass(command_buffer->GetHandle());
		} else {
			assert(pass_cmd.subpasses.size() == 1);
			run_pass(pass_cmd.subpasses.front());
		}
	}
	cmd_pipeline_barriers(vk_command.GetPostBarriers());
}

} // namespace myvk_rg_executor
