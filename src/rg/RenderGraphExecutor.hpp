#ifndef MYVK_RG_RENDER_GRAPH_EXECUTOR_HPP
#define MYVK_RG_RENDER_GRAPH_EXECUTOR_HPP

#include "RenderGraphAllocator.hpp"
#include "RenderGraphScheduler.hpp"

#include <myvk/ImagelessFramebuffer.hpp>
#include <myvk/RenderPass.hpp>

#include <algorithm>

namespace myvk_rg::_details_ {

class RenderGraphExecutor {
private:
	struct MemoryBarrier {
		VkPipelineStageFlags2 src_stage_mask;
		VkAccessFlags2 src_access_mask;
		VkPipelineStageFlags2 dst_stage_mask;
		VkAccessFlags2 dst_access_mask;
	};
	struct BufferMemoryBarrier : public MemoryBarrier {
		const BufferBase *buffer;
	};
	struct ImageMemoryBarrier : public MemoryBarrier {
		const ImageBase *image;
		VkImageLayout old_layout;
		VkImageLayout new_layout;
	};

	struct AttachmentInfo {
		struct AttachmentReference {
			const Input *p_input{};
			uint32_t subpass{};
		};
		const ImageBase *image{};
		std::vector<AttachmentReference> references;
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

	struct SubpassDependencies;

	void reset_pass_executor_vector(const RenderGraphScheduler &scheduled);
	std::vector<SubpassDependencies> extract_barriers_and_subpass_dependencies(const RenderGraphResolver &resolved,
	                                                                           const RenderGraphScheduler &scheduled,
	                                                                           const RenderGraphAllocator &allocated);
	void create_render_passes_and_framebuffers(const myvk::Ptr<myvk::Device> &device,
	                                           std::vector<SubpassDependencies> &&subpass_dependencies,
	                                           const RenderGraphScheduler &scheduled,
	                                           const RenderGraphAllocator &allocated);

public:
	void Prepare(const myvk::Ptr<myvk::Device> &device, const RenderGraphResolver &resolved,
	             const RenderGraphScheduler &scheduled, const RenderGraphAllocator &allocated);

	void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const;
};

} // namespace myvk_rg::_details_

#endif
