#ifndef MYVK_RG_RENDER_GRAPH_BASE_HPP
#define MYVK_RG_RENDER_GRAPH_BASE_HPP

#include <myvk/DescriptorSet.hpp>
#include <myvk/DeviceObjectBase.hpp>

#include <list>
#include <memory>
#include <unordered_set>

#include "Usage.hpp"

namespace myvk_rg::_details_ {

class PassBase;
namespace _details_rg_pool_ {
using ResultPoolData = PoolData<ResourceBase *>;
}
class ImageBase;
class ManagedBuffer;
class CombinedImage;

class RenderGraphAllocation;
class RenderGraphBase : public myvk::DeviceObjectBase {
private:
	myvk::Ptr<myvk::Device> m_device_ptr;
	const _details_rg_pool_::ResultPoolData *m_p_result_pool_data{};
	VkExtent2D m_canvas_size{};
	VkPipelineStageFlags2 m_src_stages{VK_PIPELINE_STAGE_2_NONE}, m_dst_stages{VK_PIPELINE_STAGE_2_NONE};
	bool m_lazy_allocation_supported{};

	struct CompilePhrase {
		enum : uint8_t {
			kResolve = 1u,
			kAllocate = 2u,
			kPrepareExecutor = 4u,
		};
	};
	uint8_t m_compile_phrase{};
	inline void set_compile_phrase(uint8_t phrase) { m_compile_phrase |= phrase; }

	struct Compiler;
	std::unique_ptr<Compiler> m_compiler{};

	void MYVK_RG_INITIALIZER_FUNC(const myvk::Ptr<myvk::Device> &device);

	template <typename> friend class RenderGraph;
	template <typename> friend class ImageAttachmentInfo;
	template <typename, typename> friend class ManagedResourceInfo;
	friend class ManagedImage;
	friend class CombinedImage;
	template <typename> friend class PassPool;
	template <typename> friend class InputPool;
	friend class RenderGraphBuffer;
	friend class RenderGraphImage;

	friend class RenderGraphResolver;
	friend class RenderGraphExecutor;

public:
	RenderGraphBase();
	~RenderGraphBase() override;

	inline void SetCanvasSize(const VkExtent2D &canvas_size) {
		if (canvas_size.width != m_canvas_size.width || canvas_size.height != m_canvas_size.height) {
			m_canvas_size = canvas_size;
			set_compile_phrase(CompilePhrase::kAllocate);
		}
	}
	inline void SetSrcStages(VkPipelineStageFlags2 src_stages) {
		if (m_src_stages != src_stages) {
			m_src_stages = src_stages;
			set_compile_phrase(CompilePhrase::kPrepareExecutor);
		}
	}
	inline void SetDstStages(VkPipelineStageFlags2 dst_stages) {
		if (m_dst_stages != dst_stages) {
			m_dst_stages = dst_stages;
			set_compile_phrase(CompilePhrase::kPrepareExecutor);
		}
	}

	inline const myvk::Ptr<myvk::Device> &GetDevicePtr() const final { return m_device_ptr; }
	void Compile();
};

} // namespace myvk_rg::_details_

#endif
