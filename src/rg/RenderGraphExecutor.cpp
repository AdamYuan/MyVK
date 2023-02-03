#include "RenderGraphExecutor.hpp"

#include <iostream>

namespace myvk_rg::_details_ {

void RenderGraphExecutor::reset_pass_executor_vector() {
	m_pass_executors.clear();
	m_pass_executors.resize(m_p_resolved->GetPassCount());

	for (uint32_t pass_id = 0; pass_id < m_p_resolved->GetPassCount(); ++pass_id)
		m_pass_executors[pass_id].p_info = &m_p_resolved->GetPassInfo(pass_id);
}

void RenderGraphExecutor::extract_barriers() {
	/* const auto extract_barriers =
	    [this](const std::vector<const RenderGraphResolver::PassDependency *> &input_dependencies) -> BarrierInfo {
		std::unordered_map<const ImageBase *, VkImageMemoryBarrier2> image_barriers;
		std::unordered_map<const BufferBase *, VkBufferMemoryBarrier2> buffer_barriers;

		for (const auto *p_dep : input_dependencies) {
			p_dep->resource->Visit([this, p_dep, &image_barriers, &buffer_barriers](const auto *resource) -> void {
				if constexpr (ResourceVisitorTrait<decltype(resource)>::kType == ResourceType::kBuffer) {
					VkBufferMemoryBarrier2 *p_barrier;
					{
						auto it = buffer_barriers.find(resource);
						if (it == buffer_barriers.end()) {
							p_barrier = &buffer_barriers.insert({resource, {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2}})
							                 .first->second;
							const myvk::Ptr<myvk::BufferBase> &myvk_buffer = m_p_allocated->GetVkBuffer(resource);
							p_barrier->buffer = myvk_buffer->GetHandle();
							p_barrier->size = myvk_buffer->GetSize();
							p_barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
							p_barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
						} else
							p_barrier = &it->second;
					}
					if (p_dep->p_input_from) {
						p_barrier->srcStageMask |= p_dep->p_input_from->GetUsagePipelineStages();
						p_barrier->srcStageMask |= UsageGetWriteAccessFlags(p_dep->p_input_from->GetUsage());
					}
					if (p_dep->p_input_to) {
						p_barrier->dstStageMask |= p_dep->p_input_to->GetUsagePipelineStages();
						p_barrier->dstAccessMask |= UsageGetAccessFlags(p_dep->p_input_to->GetUsage());
					}
				} else {
					VkImageMemoryBarrier2 *p_barrier;
					{
						auto it = image_barriers.find(resource);
						if (it == image_barriers.end()) {
							p_barrier = &image_barriers.insert({resource, {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2}})
							                 .first->second;
							const myvk::Ptr<myvk::ImageView> &myvk_image_view = m_p_allocated->GetVkImageView(resource);
							p_barrier->image = myvk_image_view->GetImagePtr()->GetHandle();
							p_barrier->subresourceRange = myvk_image_view->GetSubresourceRange();
							p_barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
							p_barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
						} else
							p_barrier = &it->second;
					}
					if (p_dep->p_input_from) {
						assert(p_barrier->oldLayout == VK_IMAGE_LAYOUT_UNDEFINED ||
						       p_barrier->oldLayout == UsageGetImageLayout(p_dep->p_input_from->GetUsage()));
						p_barrier->oldLayout = UsageGetImageLayout(p_dep->p_input_from->GetUsage());
						p_barrier->srcStageMask |= p_dep->p_input_from->GetUsagePipelineStages();
						p_barrier->srcStageMask |= UsageGetWriteAccessFlags(p_dep->p_input_from->GetUsage());
					}
					if (p_dep->p_input_to) {
						assert(p_barrier->newLayout == VK_IMAGE_LAYOUT_UNDEFINED ||
						       p_barrier->newLayout == UsageGetImageLayout(p_dep->p_input_to->GetUsage()));
						p_barrier->newLayout = UsageGetImageLayout(p_dep->p_input_to->GetUsage());
						p_barrier->dstStageMask |= p_dep->p_input_to->GetUsagePipelineStages();
						p_barrier->dstAccessMask |= UsageGetAccessFlags(p_dep->p_input_to->GetUsage());
					}
				}
			});
		}
		BarrierInfo barrier_info = {};
		barrier_info.buffer_barriers.reserve(buffer_barriers.size());
		barrier_info.image_barriers.reserve(image_barriers.size());
		for (const auto &it : buffer_barriers)
			barrier_info.buffer_barriers.push_back(it.second);
		for (const auto &it : image_barriers)
			barrier_info.image_barriers.push_back(it.second);

		// Set Render Graph Specified src and dst Stage Mask
		for (auto &buffer_barrier : barrier_info.buffer_barriers) {
			if (buffer_barrier.srcStageMask == 0)
				buffer_barrier.srcStageMask = m_p_render_graph->m_src_stages;
			else if (buffer_barrier.dstStageMask == 0)
				buffer_barrier.dstStageMask = m_p_render_graph->m_dst_stages;
		}
		for (auto &image_barrier : barrier_info.image_barriers) {
			if (image_barrier.srcStageMask == 0)
				image_barrier.srcStageMask = m_p_render_graph->m_src_stages;
			else if (image_barrier.dstStageMask == 0)
				image_barrier.dstStageMask = m_p_render_graph->m_dst_stages;
		}

		return barrier_info;
	};

	for (auto &pass_executor : m_pass_executors) {
		std::cout << pass_executor.p_info->subpasses[0].pass->GetKey().GetName() << std::endl;
		pass_executor.barrier_info = extract_barriers(pass_executor.p_info->input_dependencies);
	}
	m_post_barrier = extract_barriers(m_p_resolved->GetPostDependencyPtrs()); */
}

void RenderGraphExecutor::extract_render_passes_and_framebuffers() {
	for (auto &pass_executor : m_pass_executors) {
	}
}

void RenderGraphExecutor::Prepare(const RenderGraphBase *p_render_graph, const RenderGraphResolver &resolved,
                                  const RenderGraphAllocator &allocated) {
	m_p_render_graph = p_render_graph;
	m_p_resolved = &resolved;
	m_p_allocated = &allocated;

	reset_pass_executor_vector();
	extract_barriers();
	extract_render_passes_and_framebuffers();
}

} // namespace myvk_rg::_details_
