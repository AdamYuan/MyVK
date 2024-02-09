#include <myvk_rg/executor/DefaultExecutor.hpp>

namespace myvk_rg::executor {

void DefaultExecutor::OnEvent(const interface::ObjectBase &object, interface::Event event) {}

void DefaultExecutor::CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const {}

} // namespace myvk_rg::executor