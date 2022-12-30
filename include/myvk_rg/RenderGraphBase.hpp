#ifndef MYVK_RG_RENDER_GRAPH_BASE_HPP
#define MYVK_RG_RENDER_GRAPH_BASE_HPP

#include "PassBase.hpp"

namespace myvk_rg {

namespace _details_rg_pool_ {
using ResultPoolData = PoolData<ResourceBase *>;
}
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

} // namespace myvk_rg

#endif
