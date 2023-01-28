#ifndef MYVK_RG_RENDER_GRAPH_EXECUTOR_HPP
#define MYVK_RG_RENDER_GRAPH_EXECUTOR_HPP

#include "RenderGraphAllocator.hpp"
#include "RenderGraphResolver.hpp"

#include <myvk/ImagelessFramebuffer.hpp>
#include <myvk/RenderPass.hpp>

namespace myvk_rg::_details_ {

class RenderGraphExecutor {
private:
	const RenderGraphBase *m_p_render_graph;
	const RenderGraphResolver *m_p_resolved;
	const RenderGraphAllocator *m_p_allocated;

	struct RenderPassInfo {
		myvk::Ptr<myvk::RenderPass> myvk_render_pass;
		myvk::Ptr<myvk::ImagelessFramebuffer> myvk_framebuffer;
	};
	struct BarrierInfo {
		std::vector<VkBufferMemoryBarrier2> buffer_barriers;
		std::vector<VkImageMemoryBarrier2> image_barriers;
		inline bool empty() const { return buffer_barriers.empty() && image_barriers.empty(); }
		inline void clear() {
			buffer_barriers.clear();
			image_barriers.clear();
		}
	};
	struct PassExecutor {
		const RenderGraphResolver::PassInfo *p_info{};
		BarrierInfo barrier_info;
		RenderPassInfo render_pass_info;
	};
	std::vector<PassExecutor> m_pass_executors;
	BarrierInfo m_post_barrier;

	void reset_pass_executor_vector();
	void extract_barriers();
	void extract_render_passes_and_framebuffers();

public:
	void Prepare(const RenderGraphBase *p_render_graph, const RenderGraphResolver &resolved,
	             const RenderGraphAllocator &allocated);

	void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer, VkPipelineStageFlags2 src) const;
};

} // namespace myvk_rg::_details_

#endif
