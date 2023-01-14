#ifndef MYVK_RG_DETAILS_RENDER_GRAPH_HPP
#define MYVK_RG_DETAILS_RENDER_GRAPH_HPP

#include "Pass.hpp"
#include "RenderGraphBase.hpp"
#include "Resource.hpp"

namespace myvk_rg::_details_ {

template <typename Derived>
class RenderGraph : public RenderGraphBase,
                    public PassPool<Derived>,
                    public ResourcePool<Derived>,
                    public Pool<Derived, ResourceBase *> {
private:
	using _ResultPool = Pool<Derived, ResourceBase *>;

protected:
	inline bool AddResult(const PoolKey &result_key, ResourceBase *resource) {
		assert(resource);
		m_compile_phrase.assign_pass_resource_indices = true;
		return _ResultPool::template CreateAndInitializeForce<0, ResourceBase *>(result_key, resource);
	}
	inline bool IsResultExist(const PoolKey &result_key) const { return _ResultPool::Exist(result_key); }
	inline void RemoveResult(const PoolKey &result_key) {
		m_compile_phrase.assign_pass_resource_indices = true;
		_ResultPool::Delete(result_key);
	}
	inline void ClearResults() {
		m_compile_phrase.assign_pass_resource_indices = true;
		_ResultPool::Clear();
	}

public:
	inline ~RenderGraph() override = default;

	template <typename... Args>
	inline static myvk::Ptr<Derived> Create(const myvk::Ptr<myvk::Device> &device_ptr, Args &&...args) {
		static_assert(std::is_base_of_v<RenderGraph<Derived>, Derived>);

		auto ret = std::make_shared<Derived>();
		dynamic_cast<RenderGraphBase *>(ret.get())->m_device_ptr = device_ptr;
		ret->MYVK_RG_INITIALIZER_FUNC(std::forward<Args>(args)...);
		return ret;
	}
	inline RenderGraph() {
		m_p_result_pool_data = &_ResultPool::GetPoolData();
		m_p_pass_pool_sequence = &PassPool<Derived>::GetPassSequence();
		// m_p_resource_pool_data = &ResourcePool<Derived>::GetPoolData();
	}
};

} // namespace myvk_rg::_details_

#endif
