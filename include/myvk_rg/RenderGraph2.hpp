#ifndef MYVK_RG_RENDER_GRAPH_HPP
#define MYVK_RG_RENDER_GRAPH_HPP

#include "../myvk/CommandBuffer.hpp"
#include "../myvk/FrameManager.hpp"
#include <cassert>
#include <climits>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>

#include "Pass.hpp"

namespace myvk_rg {

namespace _details_rg_pool_ {
using ResultPoolData = PoolData<ResourceBase *>;
}
template <typename Derived> class ResultPool : public Pool<Derived, ResourceBase *> {
private:
	using _ResultPool = Pool<Derived, ResourceBase *>;

public:
	inline ResultPool() = default;
	inline ResultPool(ResultPool &&) noexcept = default;
	inline ~ResultPool() override = default;

protected:
	inline bool AddResult(const PoolKey &result_key, ResourceBase *resource) {
		assert(resource);
		return _ResultPool::template CreateAndInitialize<0, ResourceBase *>(result_key, resource);
	}
	inline bool IsResultExist(const PoolKey &result_key) const { return _ResultPool::Exist(result_key); }
	inline void RemoveResult(const PoolKey &result_key) { _ResultPool::Delete(result_key); }
	inline void ClearResults() { _ResultPool::Clear(); }
};

class RenderGraphBase : public myvk::DeviceObjectBase {
private:
	myvk::Ptr<myvk::Device> m_device_ptr;
	uint32_t m_frame_count{};
	VkExtent2D m_canvas_size{};

	const std::vector<PassBase *> *m_p_pass_pool_sequence{};
	const _details_rg_pool_::ResultPoolData *m_p_result_pool_data{};
	// const _details_rg_pool_::ResourcePoolData *m_p_resource_pool_data{};
	// const _details_rg_pool_::PassPoolData *m_p_pass_pool_data{};

	mutable std::vector<PassBase *> m_pass_sequence;
	void _visit_pass_graph(PassBase *pass) const;
	void _extract_visited_pass(const std::vector<PassBase *> *p_cur_seq) const;

	bool m_pass_graph_updated = true, m_resource_updated = true;

	template <typename> friend class RenderGraph;

protected:
	inline void SetFrameCount(uint32_t frame_count) {
		if (m_frame_count != frame_count)
			m_resource_updated = true;
		m_frame_count = frame_count;
	}
	inline void SetCanvasSize(const VkExtent2D &canvas_size) {}

public:
	inline const myvk::Ptr<myvk::Device> &GetDevicePtr() const final { return m_device_ptr; }
	inline uint32_t GetFrameCount() const { return m_frame_count; }
	void gen_pass_sequence() const;
};

template <typename Derived>
class RenderGraph : public RenderGraphBase,
                    public PassPool<Derived>,
                    public ResourcePool<Derived>,
                    public ResultPool<Derived> {
public:
	template <typename... Args>
	inline static myvk::Ptr<Derived> Create(const myvk::Ptr<myvk::Device> &device_ptr, Args &&...args) {
		static_assert(std::is_base_of_v<RenderGraph<Derived>, Derived>);

		auto ret = std::make_shared<Derived>(std::forward<Args>(args)...);
		dynamic_cast<RenderGraphBase *>(ret.get())->m_device_ptr = device_ptr;
		return ret;
	}
	inline RenderGraph() {
		m_p_result_pool_data = &ResultPool<Derived>::GetPoolData();
		m_p_pass_pool_sequence = &PassPool<Derived>::GetPassSequence();
		// m_p_resource_pool_data = &ResourcePool<Derived>::GetPoolData();
	}
};

// TODO: Debug Type Traits
static_assert(std::is_same_v<PoolVariant<BufferAlias, ObjectBase, ResourceBase, ImageAlias>,
                             std::variant<std::monostate, ImageAlias, std::unique_ptr<ResourceBase>,
                                          std::unique_ptr<ObjectBase>, BufferAlias>>);
} // namespace myvk_rg

#endif
