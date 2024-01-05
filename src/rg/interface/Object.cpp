#include <myvk_rg/interface/Object.hpp>

#include <myvk_rg/interface/Executor.hpp>
#include <myvk_rg/interface/RenderGraph.hpp>

namespace myvk_rg::interface {

void ObjectBase::EmitEvent(Event event) const { GetRenderGraphPtr()->m_executor->OnEvent(*this, event); }

} // namespace myvk_rg::interface