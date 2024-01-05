#ifndef MYVK_RG_DETAILS_RENDER_GRAPH_HPP
#define MYVK_RG_DETAILS_RENDER_GRAPH_HPP

#include "Executor.hpp"
#include "Object.hpp"
#include "Pass.hpp"
#include "Resource.hpp"
#include "ResultPool.hpp"
#include <memory>

namespace myvk_rg::interface {

class RenderGraphBase : public ObjectBase,
                        public PassPool<RenderGraphBase>,
                        public ResourcePool<RenderGraphBase>,
                        public ResultPool<RenderGraphBase> {
private:
	inline static const PoolKey kRGKey = {"[RG]"}, kEXEKey = {"[EXE]"};

	VkExtent2D m_canvas_size{};
	std::unique_ptr<ExecutorBase> m_executor{};

	friend class ObjectBase;

public:
	inline RenderGraphBase() : ObjectBase({.p_pool_key = &kRGKey, .p_var_parent = this}) {}
	inline ~RenderGraphBase() override = default;

	inline void SetCanvasSize(const VkExtent2D &canvas_size) {
		if (canvas_size.width != m_canvas_size.width || canvas_size.height != m_canvas_size.height) {
			m_canvas_size = canvas_size;
			EmitEvent(Event::kCanvasResized);
		}
	}
	inline const VkExtent2D &GetCanvasSize() const { return m_canvas_size; }

	template <typename RGType, typename... Args> inline static myvk::Ptr<RGType> Create(Args &&...args) {
		static_assert(std::is_base_of_v<RenderGraphBase, RGType>);
		return std::make_shared<RGType>(std::forward<Args>(args)...);
	}
	template <typename EXEType, typename... Args> inline void SetExecutor(Args &&...args) {
		m_executor = std::make_unique<EXEType>(Parent{.p_pool_key = &kEXEKey, .p_var_parent = this},
		                                       std::forward<Args>(args)...);
	}
	template <typename EXEType> inline const EXEType *GetExecutor() {
		static_assert(std::is_base_of_v<ExecutorBase, EXEType>);
		return static_cast<const EXEType *>(m_executor.get());
	}
};

} // namespace myvk_rg::interface

#endif
