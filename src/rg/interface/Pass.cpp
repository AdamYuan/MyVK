#include <myvk_rg/interface/Pass.hpp>

#include <myvk_rg/interface/RenderGraph.hpp>

namespace myvk_rg::interface {

uint32_t GraphicsPassBase::GetSubpass() const { return executor::Executor::GetSubpass(this); }
const myvk::Ptr<myvk::RenderPass> &GraphicsPassBase::GetVkRenderPass() const {
	return GetRenderGraphPtr()->GetExecutor()->GetVkRenderPass(this);
}

} // namespace myvk_rg::interface
