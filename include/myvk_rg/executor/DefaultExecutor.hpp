#pragma once
#ifndef MYVK_RG_DEFAULT_EXECUTOR_HPP
#define MYVK_RG_DEFAULT_EXECUTOR_HPP

#include <myvk_rg/interface/Executor.hpp>

namespace myvk_rg::executor {

class DefaultExecutor final : public interface::ExecutorBase {
private:
	enum CompilePhrase : uint8_t {
		kResolve = 1u,
		kCreateDescriptor = 2u,
		kSchedule = 4u,
		kAllocate = 8u,
		kPrepareExecutor = 16u,
		kPreBindDescriptor = 32u,
		kInitLastFrameResource = 64u
	};

public:
	inline DefaultExecutor(interface::Parent parent) : interface::ExecutorBase(parent) {}
	inline ~DefaultExecutor() final = default;

	void OnEvent(const interface::ObjectBase &object, interface::Event event) final;
};

} // namespace myvk_rg::executor

#endif
