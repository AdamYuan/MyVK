#ifndef MYVK_RG_RENDER_GRAPH_BASE_HPP
#define MYVK_RG_RENDER_GRAPH_BASE_HPP

#include <myvk/DescriptorSet.hpp>
#include <myvk/DeviceObjectBase.hpp>

#include <unordered_set>

namespace myvk_rg::_details_ {

class PassBase;
namespace _details_rg_pool_ {
using ResultPoolData = PoolData<ResourceBase *>;
}
class ImageBase;
class ManagedBuffer;
class CombinedImage;
class RenderGraphBase : public myvk::DeviceObjectBase {
private:
	myvk::Ptr<myvk::Device> m_device_ptr;
	uint32_t m_frame_count{};
	VkExtent2D m_canvas_size{};

	const std::vector<PassBase *> *m_p_pass_pool_sequence{};
	const _details_rg_pool_::ResultPoolData *m_p_result_pool_data{};
	// const _details_rg_pool_::ResourcePoolData *m_p_resource_pool_data{};
	// const _details_rg_pool_::PassPoolData *m_p_pass_pool_data{};

	// The Compiler
#pragma region The Compiler
	struct CompilePhrase {
		enum : uint8_t {
			kAssignPassResourceIndices = 1u,
			kMergeSubpass = 2u,
			kGenerateVkResource = 4u,
			kGenerateVkImageView = 8u,
			kGenerateVkRenderPass = 16u,
			kGenerateVkDescriptor = 32u
		};
	};
	uint8_t m_compile_phrase{};
	inline void set_compile_phrase(uint8_t phrase) { m_compile_phrase |= phrase; }

	struct PassInfo {
		const PassBase *pass{};
		uint32_t _merge_length_{};
		uint32_t render_pass_id{};
		std::vector<myvk::Ptr<myvk::DescriptorSet>> myvk_descriptor_sets;
	};
	struct RenderPassInfo {
		uint32_t first_pass, last_pass;
		VkFramebuffer vk_framebuffer{VK_NULL_HANDLE};
		VkRenderPass vk_render_pass{VK_NULL_HANDLE};
	};
	mutable struct {
		// Phrase: assign_pass_resource_indices
		std::unordered_set<const ImageBase *> _managed_image_set_;      // Temporally used
		std::unordered_set<const ManagedBuffer *> _managed_buffer_set_; // Temporally used
		std::vector<PassInfo> passes;                                   // Major Pass Sequence
		std::vector<const ImageBase *> managed_images;                  // Contains CombinedImage and ManagedImage
		std::vector<const ManagedBuffer *> managed_buffers;             // Contains ManagedBuffer
		std::vector<RenderPassInfo> render_passes;
	} m_compile_info{};
	void _visit_resource_dep_pass(const ResourceBase *resource) const;
	void _extract_visited_pass(const std::vector<PassBase *> *p_cur_seq) const;
	static void _traverse_combined_image(const CombinedImage *image);
	// Compile Phrase Functions
	void assign_pass_resource_indices() const;
	void merge_subpass() const;
	void generate_vk_resource() const;
#pragma endregion

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
			set_compile_phrase(CompilePhrase::kAssignPassResourceIndices);
		m_frame_count = frame_count;
	}
	inline void SetCanvasSize(const VkExtent2D &canvas_size) {}

public:
	inline RenderGraphBase() = default;
	inline ~RenderGraphBase() override = default;

	inline const myvk::Ptr<myvk::Device> &GetDevicePtr() const final { return m_device_ptr; }
	inline uint32_t GetFrameCount() const { return m_frame_count; }
	inline void Compile() {
		switch (m_compile_phrase & -m_compile_phrase) { // Switch with Lowest Bit
		case CompilePhrase::kAssignPassResourceIndices:
			assign_pass_resource_indices();
		case CompilePhrase::kMergeSubpass:
			merge_subpass();
		case CompilePhrase::kGenerateVkResource:
			generate_vk_resource();
		}
		m_compile_phrase = 0u;
	}
};

} // namespace myvk_rg::_details_

#endif
