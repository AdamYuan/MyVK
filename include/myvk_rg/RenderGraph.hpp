#ifndef MYVK_RG_RENDER_GRAPH_HPP
#define MYVK_RG_RENDER_GRAPH_HPP

#include "Pass.hpp"
#include "RenderGraphBase.hpp"
#include "Resource.hpp"

namespace myvk_rg {

template <typename Derived>
class RenderGraph : public RenderGraphBase,
                    public PassPool<Derived>,
                    public ResourcePool<Derived>,
                    public Pool<Derived, ResourceBase *> {
private:
	using _ResultPool = Pool<Derived, ResourceBase *>;

public:
	template <typename... Args>
	inline static myvk::Ptr<Derived> Create(const myvk::Ptr<myvk::Device> &device_ptr, Args &&...args) {
		static_assert(std::is_base_of_v<RenderGraph<Derived>, Derived>);

		auto ret = std::make_shared<Derived>(std::forward<Args>(args)...);
		dynamic_cast<RenderGraphBase *>(ret.get())->m_device_ptr = device_ptr;
		return ret;
	}
	inline RenderGraph() {
		m_p_result_pool_data = &_ResultPool::GetPoolData();
		m_p_pass_pool_sequence = &PassPool<Derived>::GetPassSequence();
		// m_p_resource_pool_data = &ResourcePool<Derived>::GetPoolData();
	}
	inline bool AddResult(const PoolKey &result_key, ResourceBase *resource) {
		assert(resource);
		return _ResultPool::template CreateAndInitialize<0, ResourceBase *>(result_key, resource);
	}
	inline bool IsResultExist(const PoolKey &result_key) const { return _ResultPool::Exist(result_key); }
	inline void RemoveResult(const PoolKey &result_key) { _ResultPool::Delete(result_key); }
	inline void ClearResults() { _ResultPool::Clear(); }
};

// TODO: Debug Type Traits
static_assert(std::is_same_v<PoolVariant<BufferAlias, ObjectBase, ResourceBase, ImageAlias>,
                             std::variant<std::monostate, ImageAlias, std::unique_ptr<ResourceBase>,
                                          std::unique_ptr<ObjectBase>, BufferAlias>>);
} // namespace myvk_rg

#endif
