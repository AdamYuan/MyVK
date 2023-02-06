#ifndef MYVK_RG_RENDER_GRAPH_BASE_HPP
#define MYVK_RG_RENDER_GRAPH_BASE_HPP

#include <myvk/CommandBuffer.hpp>
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

enum class CompilePhrase : uint8_t {
	kResolve = 1u,
	kSchedule = 2u,
	kAllocate = 4u,
	kPrepareExecutor = 8u,
};
inline constexpr CompilePhrase operator|(CompilePhrase x, CompilePhrase y) {
	return static_cast<CompilePhrase>(static_cast<uint8_t>(x) | static_cast<uint8_t>(y));
}

class RenderGraphBase : public myvk::DeviceObjectBase {
public:
	inline void SetCompilePhrases(CompilePhrase phrase) const { m_compile_phrase |= static_cast<uint8_t>(phrase); }

private:
	myvk::Ptr<myvk::Device> m_device_ptr;
	const _details_rg_pool_::ResultPoolData *m_p_result_pool_data{};
	VkExtent2D m_canvas_size{};
	bool m_lazy_allocation_supported{};

	mutable uint8_t m_compile_phrase{};
	inline void SetCompilePhrases(uint8_t phrase) { m_compile_phrase |= phrase; }

	struct Compiler;
	std::unique_ptr<Compiler> m_compiler{};

	void MYVK_RG_INITIALIZER_FUNC(const myvk::Ptr<myvk::Device> &device);

	template <typename> friend class RenderGraph;
	template <typename> friend class ImageAttachmentInfo;
	friend class ManagedBuffer;
	friend class ManagedImage;
	friend class CombinedImage;
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
			SetCompilePhrases(CompilePhrase::kAllocate | CompilePhrase::kSchedule);
		}
	}
	inline const VkExtent2D &GetCanvasSize() const { return m_canvas_size; }

	void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const;

	inline const myvk::Ptr<myvk::Device> &GetDevicePtr() const final { return m_device_ptr; }
	void compile() const; // TODO: Mark it as private
};

} // namespace myvk_rg::_details_

#endif
