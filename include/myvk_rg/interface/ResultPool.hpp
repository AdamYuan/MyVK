#pragma once
#ifndef MYVK_RG_RESULT_POOL_HPP
#define MYVK_RG_RESULT_POOL_HPP

#include "Alias.hpp"
#include "Pool.hpp"

namespace myvk_rg::interface {

template <typename Derived> class ResultPool : public Pool<Derived, Variant<OutputBufferAlias, OutputImageAlias>> {
private:
	using PoolBase = Pool<Derived, Variant<OutputBufferAlias, OutputImageAlias>>;

public:
	inline ResultPool() = default;
	inline ~ResultPool() override = default;

	inline const auto &GetResultPoolData() const { return PoolBase::GetPoolData(); }

protected:
	inline void AddResult(const PoolKey &result_key, const OutputImageAlias &image) {
		static_cast<const ObjectBase *>(static_cast<const Derived *>(this))->EmitEvent(Event::kResultChanged);
		PoolBase::template Construct<0, OutputImageAlias>(result_key, image);
	}
	inline void AddResult(const PoolKey &result_key, const OutputBufferAlias &buffer) {
		static_cast<const ObjectBase *>(static_cast<const Derived *>(this))->EmitEvent(Event::kResultChanged);
		PoolBase::template Construct<0, OutputBufferAlias>(result_key, buffer);
	}
	inline void ClearResults() {
		static_cast<const ObjectBase *>(static_cast<const Derived *>(this))->EmitEvent(Event::kResultChanged);
		PoolBase::Clear();
	}
};

} // namespace myvk_rg::interface

#endif // MYVK_RESULTPOOL_HPP
