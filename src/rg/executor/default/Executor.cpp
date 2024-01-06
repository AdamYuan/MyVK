#include <myvk_rg/executor/DefaultExecutor.hpp>

namespace myvk_rg::executor {

void DefaultExecutor::OnEvent(const interface::ObjectBase &object, interface::Event event) {}

interface::CompileResult<void> DefaultExecutor::CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const {
	return {};
}

} // namespace myvk_rg::executor