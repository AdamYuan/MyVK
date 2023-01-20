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

class RenderGraphAllocation;
class RenderGraphBase : public myvk::DeviceObjectBase {
private:
	myvk::Ptr<myvk::Device> m_device_ptr;
	VkExtent2D m_canvas_size{};

	const std::vector<PassBase *> *m_p_pass_pool_sequence{};
	const _details_rg_pool_::ResultPoolData *m_p_result_pool_data{};

	// The Compiler
#pragma region The Compiler
	struct CompilePhrase {
		enum : uint8_t {
			kAssignPassResourceIndices = 1u,
			kMergeSubpass = 2u,
			kGenerateVkResource = 8u,
			kGenerateVkImageView = 16u,
			kGenerateVkRenderPass = 32u,
			kGenerateVkDescriptor = 64u
		};
	};
	uint8_t m_compile_phrase{};
	inline void set_compile_phrase(uint8_t phrase) { m_compile_phrase |= phrase; }

	struct PassInfo {
		const PassBase *pass{};
		uint32_t _merge_length_{};
		uint32_t render_pass_id{};
		myvk::Ptr<myvk::DescriptorSet> myvk_descriptor_set{};
	};
	struct InternalResourceInfo {
		VkMemoryRequirements vk_memory_requirements{};
		uint32_t first_pass{}, last_pass{}; // lifespan
		uint32_t allocation_id{};
		VkDeviceSize memory_offset{}; // Created by _calculate_memory_allocation
	};
	struct InternalImageInfo final : public InternalResourceInfo {
		const ImageBase *image{};
		myvk::Ptr<myvk::ImageBase> myvk_image{};
		VkImageUsageFlags vk_image_usages{};
		VkImageType vk_image_type{}; // Union of 1 << VK_IMAGE_TYPE_xD
		bool is_transient{};
	};
	struct InternalBufferInfo final : public InternalResourceInfo {
		const ManagedBuffer *buffer{};
		myvk::Ptr<myvk::BufferBase> myvk_buffer{};
		VkBufferUsageFlags vk_buffer_usages{};
	};
	struct RenderPassInfo {
		uint32_t first_pass{}, last_pass{};
		VkFramebuffer vk_framebuffer{VK_NULL_HANDLE};
		VkRenderPass vk_render_pass{VK_NULL_HANDLE};
	};
	struct AllocationInfo {
		myvk::Ptr<RenderGraphAllocation> myvk_allocation{};
	};
	struct MemoryInfo {
		std::vector<InternalResourceInfo *> resources;
		VkDeviceSize alignment = 1;
		uint32_t memory_type_bits = -1;
		inline void push(InternalResourceInfo *resource) {
			resources.push_back(resource);
			alignment = std::max(alignment, resource->vk_memory_requirements.alignment);
			memory_type_bits &= resource->vk_memory_requirements.memoryTypeBits;
		}
		inline bool empty() const { return resources.empty(); }
	};
	mutable struct {
		// Phrase: assign_pass_resource_indices
		std::unordered_set<const ImageBase *> _internal_image_set_;      // Temporally used
		std::unordered_set<const ManagedBuffer *> _internal_buffer_set_; // Temporally used
		std::vector<PassInfo> passes;                                    // Major Pass Sequence
		std::vector<InternalImageInfo> internal_images;                  // Contains CombinedImage and ManagedImage
		std::vector<InternalBufferInfo> internal_buffers;                // Contains ManagedBuffer
		// Phrase: merge_subpass
		std::vector<RenderPassInfo> render_passes;
		// Phrase: generate_vk_resource
		std::vector<AllocationInfo> allocations;
	} m_compile_info{};
	// Compile Phrase Functions
	// 1: Assign Pass & Resource Indices
	void _visit_resource_dep_passes(const ResourceBase *resource) const;
	void _extract_visited_passes(const std::vector<PassBase *> *p_cur_seq) const;
	static void _initialize_combined_image(const CombinedImage *image);
	void assign_pass_resource_indices() const;
	// 2: Merge Subpass
	void _compute_merge_length() const;
	void _compute_resource_property_and_lifespan() const; // right after merge_subpass
	void merge_subpass() const;
	// 3: Generate Vulkan Resource
	static void _maintain_combined_image_size(const CombinedImage *image);
	void _create_vk_resource() const;
	void _make_naive_allocation(MemoryInfo &&memory_info, const VmaAllocationCreateInfo &allocation_create_info) const;
	void _make_optimal_allocation(MemoryInfo &&memory_info,
	                              const VmaAllocationCreateInfo &allocation_create_info) const;
	void _create_and_bind_memory_allocation() const;
	void generate_vk_resource() const;
#pragma endregion

	template <typename> friend class RenderGraph;
	template <typename> friend class ImageAttachmentInfo;
	template <typename, typename> friend class ManagedResourceInfo;
	friend class ManagedImage;
	friend class CombinedImage;
	template <typename> friend class PassPool;
	template <typename> friend class InputPool;
	friend class RenderGraphBuffer;
	friend class RenderGraphImage;

public:
	inline RenderGraphBase() = default;
	inline ~RenderGraphBase() override = default;

	inline void SetCanvasSize(const VkExtent2D &canvas_size) {
		if (canvas_size.width != m_canvas_size.width || canvas_size.height != m_canvas_size.height) {
			m_canvas_size = canvas_size;
			set_compile_phrase(CompilePhrase::kGenerateVkResource);
		}
	}

	inline const myvk::Ptr<myvk::Device> &GetDevicePtr() const final { return m_device_ptr; }
	inline void Compile() {
		if (m_compile_phrase == 0u)
			return;
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
