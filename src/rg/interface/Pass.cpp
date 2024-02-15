#include <myvk_rg/interface/Pass.hpp>

#include <myvk_rg/interface/RenderGraph.hpp>

namespace myvk_rg::interface {

uint32_t GraphicsPassBase::GetSubpass() const { return executor::Executor::GetSubpass(this); }
const myvk::Ptr<myvk::RenderPass> &GraphicsPassBase::GetVkRenderPass() const {
	return GetRenderGraphPtr()->GetExecutor()->GetVkRenderPass(this);
}
const myvk::Ptr<myvk::DescriptorSetLayout> &GraphicsPassBase::GetVkDescriptorSetLayout() const {
	return GetRenderGraphPtr()->GetExecutor()->GetVkDescriptorSetLayout(this);
}
const myvk::Ptr<myvk::DescriptorSet> &GraphicsPassBase::GetVkDescriptorSet() const {
	return GetRenderGraphPtr()->GetExecutor()->GetVkDescriptorSet(this);
}
const myvk::Ptr<myvk::DescriptorSetLayout> &ComputePassBase::GetVkDescriptorSetLayout() const {
	return GetRenderGraphPtr()->GetExecutor()->GetVkDescriptorSetLayout(this);
}
const myvk::Ptr<myvk::DescriptorSet> &ComputePassBase::GetVkDescriptorSet() const {
	return GetRenderGraphPtr()->GetExecutor()->GetVkDescriptorSet(this);
}

} // namespace myvk_rg::interface
