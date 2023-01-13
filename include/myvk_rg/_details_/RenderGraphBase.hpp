#ifndef MYVK_RG_RENDER_GRAPH_BASE_HPP
#define MYVK_RG_RENDER_GRAPH_BASE_HPP

#include <myvk/DeviceObjectBase.hpp>

#include <unordered_set>

namespace myvk_rg::_details_ {

class PassBase;
namespace _details_rg_pool_ {
using ResultPoolData = PoolData<ResourceBase *>;
}
class ImageBase;
class ManagedBuffer;
class RenderGraphBase : public myvk::DeviceObjectBase {
private:
	myvk::Ptr<myvk::Device> m_device_ptr;
	uint32_t m_frame_count{};
	VkExtent2D m_canvas_size{};

	const std::vector<PassBase *> *m_p_pass_pool_sequence{};
	const _details_rg_pool_::ResultPoolData *m_p_result_pool_data{};
	// const _details_rg_pool_::ResourcePoolData *m_p_resource_pool_data{};
	// const _details_rg_pool_::PassPoolData *m_p_pass_pool_data{};

	mutable struct {
		// Phrase: assign_pass_resource_indices
		std::unordered_set<ImageBase *> _managed_image_set_;      // Temporally used
		std::unordered_set<ManagedBuffer *> _managed_buffer_set_; // Temporally used
		std::vector<PassBase *> pass_sequence;   // Major Pass Sequence
		std::vector<ImageBase *> managed_images; // Contains CombinedImage and ManagedImage
		std::vector<ManagedBuffer *> managed_buffers;
	} m_compile_info{};
	void _traverse_pass_graph(PassBase *sub_image) const;
	void _extract_visited_pass(const std::vector<PassBase *> *p_cur_seq) const;
	static void _traverse_combined_image(const CombinedImage *image);

	struct {
		bool assign_pass_resource_indices : 1, merge_subpass : 1, generate_vk_resource : 1, generate_vk_image_view : 1,
		    generate_vk_render_pass : 1, generate_vk_descriptor : 1;
	} m_compile_phrase{};
	void assign_pass_resource_indices() const;

	template <typename> friend class RenderGraph;
	template <typename> friend class ImageAttachmentInfo;
	template <typename, typename> friend class ManagedResourceInfo;
	friend class ManagedImage;
	friend class CombinedImage;
	template <typename> friend class PassPool;
	template <typename> friend class InputPool;

protected:
	inline void SetFrameCount(uint32_t frame_count) {
		if (m_frame_count != frame_count)
			m_compile_phrase.assign_pass_resource_indices = true;
		m_frame_count = frame_count;
	}
	inline void SetCanvasSize(const VkExtent2D &canvas_size) {}

public:
	inline RenderGraphBase() = default;
	inline ~RenderGraphBase() override = default;

	inline const myvk::Ptr<myvk::Device> &GetDevicePtr() const final { return m_device_ptr; }
	inline uint32_t GetFrameCount() const { return m_frame_count; }
	inline void Compile() {
		if (m_compile_phrase.assign_pass_resource_indices) {
			assign_pass_resource_indices();
			m_compile_phrase.assign_pass_resource_indices = false;
			m_compile_phrase.merge_subpass = true;
		}
	}
};

} // namespace myvk_rg::_details_

#endif
