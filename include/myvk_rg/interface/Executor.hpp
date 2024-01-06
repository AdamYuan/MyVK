#pragma once
#ifndef MYVK_RG_EXECUTOR_HPP
#define MYVK_RG_EXECUTOR_HPP

#include "Object.hpp"

namespace myvk_rg::interface {

class ExecutorBase : public ObjectBase {
public:
	inline ExecutorBase(Parent parent) : ObjectBase(parent) {}
	virtual ~ExecutorBase() = default;

	virtual void OnEvent(const ObjectBase &object, Event event) = 0;
};

} // namespace myvk_rg::interface

#endif // MYVK_EXECUTOR_HPP
