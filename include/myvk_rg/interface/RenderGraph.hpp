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
	inline static const PoolKey kRGKey = {"[RG]"};

	VkExtent2D m_canvas_size{};
	myvk::UPtr<ExecutorBase> m_executor{};

	friend class ObjectBase;

public:
	inline explicit RenderGraphBase(myvk::UPtr<ExecutorBase> executor)
	    : ObjectBase({.p_pool_key = &kRGKey, .p_var_parent = this}), m_executor(std::move(executor)) {}
	inline ~RenderGraphBase() override = default;

	RenderGraphBase(const RenderGraphBase &) = delete;
	RenderGraphBase &operator=(const RenderGraphBase &) = delete;
	RenderGraphBase(RenderGraphBase &&) = delete;
	RenderGraphBase &operator=(RenderGraphBase &&) = delete;

	inline void SetCanvasSize(const VkExtent2D &canvas_size) {
		if (canvas_size.width != m_canvas_size.width || canvas_size.height != m_canvas_size.height) {
			m_canvas_size = canvas_size;
			EmitEvent(Event::kCanvasResized);
		}
	}
	inline const VkExtent2D &GetCanvasSize() const { return m_canvas_size; }
	inline const ExecutorBase *GetExecutor() const { return m_executor.get(); }
};

} // namespace myvk_rg::interface

#endif
