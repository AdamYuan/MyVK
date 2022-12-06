#ifndef SCALABLERTGI_RENDERGRAPH_HPP
#define SCALABLERTGI_RENDERGRAPH_HPP

#include "CommandBuffer.hpp"
#include "FrameManager.hpp"
#include "RenderGraphScheduler.hpp"
#include <cassert>
#include <functional>
#include <optional>
#include <utility>
#include <variant>

namespace myvk {

class RenderGraphPassBase;

enum class RenderGraphInputUsage {
	NONE,
	kPreserve,
	kColorAttachment,
	kDepthAttachmentR,
	kDepthAttachmentRW,
	kInputAttachment,
	kPresent,
	kSampledImage,
	kStorageImageR,
	kStorageImageW,
	kStorageImageRW,
	kUniformBuffer,
	kStorageBufferR,
	kStorageBufferW,
	kStorageBufferRW,
	kIndexBuffer,
	kVertexBuffer,
	kTransferSrc,
	kTransferDst
};

#define CASE(NAME) case RenderGraphInputUsage::NAME:
inline constexpr bool RenderGraphInputUsageIsDescriptor(RenderGraphInputUsage usage) {
	switch (usage) {
		CASE(kInputAttachment)
		CASE(kSampledImage)
		CASE(kStorageImageR)
		CASE(kStorageImageW)
		CASE(kStorageImageRW)
		CASE(kUniformBuffer)
		CASE(kStorageBufferR)
		CASE(kStorageBufferW)
		CASE(kStorageBufferRW)
		return true;
	default:
		return false;
	}
}

inline constexpr VkDescriptorType RenderGraphInputUsageGetDescriptorType(RenderGraphInputUsage usage) {
	switch (usage) {
		CASE(kInputAttachment)
		return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
		CASE(kSampledImage)
		return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		CASE(kStorageImageR)
		CASE(kStorageImageW)
		CASE(kStorageImageRW)
		return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		CASE(kUniformBuffer)
		return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		CASE(kStorageBufferR)
		CASE(kStorageBufferW)
		CASE(kStorageBufferRW)
		return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	default:
		return VK_DESCRIPTOR_TYPE_MAX_ENUM;
	}
}

inline constexpr bool RenderGraphInputUsageIsReadOnly(RenderGraphInputUsage usage) {
	switch (usage) {
		CASE(kPresent)
		CASE(kPreserve)
		CASE(kDepthAttachmentR)
		CASE(kInputAttachment)
		CASE(kSampledImage)
		CASE(kStorageImageR)
		CASE(kUniformBuffer)
		CASE(kStorageBufferR)
		CASE(kIndexBuffer)
		CASE(kVertexBuffer)
		CASE(kTransferSrc)
		return true;
	default:
		return false;
	}
}

inline constexpr bool RenderGraphInputUsageForBuffer(RenderGraphInputUsage usage) {
	switch (usage) {
		CASE(kPreserve)
		CASE(kUniformBuffer)
		CASE(kStorageBufferR)
		CASE(kStorageBufferW)
		CASE(kStorageBufferRW)
		CASE(kIndexBuffer)
		CASE(kVertexBuffer)
		CASE(kTransferSrc)
		CASE(kTransferDst)
		return true;
	default:
		return false;
	}
}

inline constexpr bool RenderGraphInputUsageForImage(RenderGraphInputUsage usage) {
	switch (usage) {
		CASE(kPreserve)
		CASE(kPresent)
		CASE(kColorAttachment)
		CASE(kDepthAttachmentR)
		CASE(kDepthAttachmentRW)
		CASE(kInputAttachment)
		CASE(kSampledImage)
		CASE(kStorageImageR)
		CASE(kStorageImageW)
		CASE(kStorageImageRW)
		CASE(kTransferSrc)
		CASE(kTransferDst)
		return true;
	default:
		return false;
	}
}

inline constexpr bool RenderGraphInputUsageGetImageLayout(RenderGraphInputUsage usage) {
	switch (usage) {
		CASE(kPresent)
		return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		CASE(kColorAttachment)
		return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		CASE(kDepthAttachmentR)
		CASE(kDepthAttachmentRW)
		return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		CASE(kInputAttachment)
		CASE(kSampledImage)
		return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		CASE(kStorageImageR)
		CASE(kStorageImageW)
		CASE(kStorageImageRW)
		return VK_IMAGE_LAYOUT_GENERAL;
		CASE(kTransferSrc)
		return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		CASE(kTransferDst)
		return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	default:
		return VK_IMAGE_LAYOUT_UNDEFINED;
	}
}

#undef CASE

class RenderGraphBase;

class RenderGraphObjectBase {
private:
	RenderGraphBase *m_render_graph_ptr;

public:
	inline explicit RenderGraphObjectBase(RenderGraphBase *render_graph_ptr) : m_render_graph_ptr{render_graph_ptr} {}
	inline virtual ~RenderGraphObjectBase() = default;

	inline RenderGraphBase *GetRenderGraphPtr() const { return m_render_graph_ptr; }

	// Disable Copy and Move
	inline RenderGraphObjectBase(const RenderGraphObjectBase &r) = delete;
	inline RenderGraphObjectBase &operator=(const RenderGraphObjectBase &r) = delete;
	inline RenderGraphObjectBase(RenderGraphObjectBase &&r) = delete;
	inline RenderGraphObjectBase &operator=(RenderGraphObjectBase &&r) = delete;
};

enum class RenderGraphResourceType { kImage, kBuffer };
enum class RenderGraphResourceState { kManaged, kExternal /*, kStatic*/ };

template <RenderGraphResourceType Type>
inline constexpr bool RenderGraphInputUsageForType(RenderGraphInputUsage usage) {
	if constexpr (Type == RenderGraphResourceType::kImage) {
		return RenderGraphInputUsageForImage(usage);
	} else {
		return RenderGraphInputUsageForBuffer(usage);
	}
}

class RenderGraphResourceBase : public RenderGraphObjectBase {
private:
	const RenderGraphPassBase *m_producer_pass_ptr;

public:
	inline explicit RenderGraphResourceBase(const RenderGraphPassBase *producer_pass_ptr)
	    : m_producer_pass_ptr{producer_pass_ptr}, RenderGraphObjectBase(producer_pass_ptr->GetRenderGraphPtr()) {}
	inline explicit RenderGraphResourceBase(RenderGraphBase *render_graph_ptr)
	    : m_producer_pass_ptr{nullptr}, RenderGraphObjectBase(render_graph_ptr) {}

	virtual RenderGraphResourceType GetType() const = 0;
	virtual RenderGraphResourceState GetState() const = 0;
	virtual bool IsAlias() const = 0;
	// virtual bool IsPerFrame() const = 0;
	// virtual void Resize(uint32_t width, uint32_t height) {}

	inline const RenderGraphPassBase *GetProducerPtr() const { return m_producer_pass_ptr; }

	~RenderGraphResourceBase() override = default;
};

/* template <typename R> struct RenderGraphResourceTraits {
    inline static constexpr bool IsResource = std::is_base_of_v<RenderGraphResourceBase, std::decay_t<R>>;
    inline static constexpr RenderGraphResourceType Type =
        std::is_base_of_v<RenderGraphResource<RenderGraphResourceType::kImage>, std::decay_t<R>>
            ? RenderGraphResourceType::kImage
            : RenderGraphResourceType::kBuffer;
    inline static constexpr bool IsImage = Type == RenderGraphResourceType::kImage;
    inline static constexpr bool IsBuffer = Type == RenderGraphResourceType::kBuffer;
    inline static constexpr RenderGraphResourceState State =
        (std::is_base_of_v<
             RenderGraphResource<RenderGraphResourceType::kImage, RenderGraphResourceState::kManaged, false>,
             std::decay_t<R>> ||
         std::is_base_of_v<
             RenderGraphResource<RenderGraphResourceType::kBuffer, RenderGraphResourceState::kManaged, false>,
             std::decay_t<R>> ||
         std::is_base_of_v<
             RenderGraphResource<RenderGraphResourceType::kImage, RenderGraphResourceState::kManaged, true>,
             std::decay_t<R>> ||
         std::is_base_of_v<
             RenderGraphResource<RenderGraphResourceType::kBuffer, RenderGraphResourceState::kManaged, true>,
             std::decay_t<R>>)
            ? RenderGraphResourceState::kManaged
            : ((std::is_base_of_v<
                    RenderGraphResource<RenderGraphResourceType::kImage, RenderGraphResourceState::kExternal, false>,
                    std::decay_t<R>> ||
                std::is_base_of_v<
                    RenderGraphResource<RenderGraphResourceType::kBuffer, RenderGraphResourceState::kExternal, false>,
                    std::decay_t<R>> ||
                std::is_base_of_v<
                    RenderGraphResource<RenderGraphResourceType::kImage, RenderGraphResourceState::kExternal, true>,
                    std::decay_t<R>> ||
                std::is_base_of_v<
                    RenderGraphResource<RenderGraphResourceType::kBuffer, RenderGraphResourceState::kExternal, true>,
                    std::decay_t<R>>)
                   ? RenderGraphResourceState::kExternal
                   : RenderGraphResourceState::NONE);
    inline static constexpr bool IsManaged = State == RenderGraphResourceState::kManaged;
    inline static constexpr bool IsExternal = State == RenderGraphResourceState::kExternal;
    inline static constexpr bool IsAlias =
        std::is_base_of_v<
            RenderGraphResource<RenderGraphResourceType::kImage, RenderGraphResourceState::kManaged, true>,
            std::decay_t<R>> ||
        std::is_base_of_v<
            RenderGraphResource<RenderGraphResourceType::kBuffer, RenderGraphResourceState::kManaged, true>,
            std::decay_t<R>> ||
        std::is_base_of_v<
            RenderGraphResource<RenderGraphResourceType::kImage, RenderGraphResourceState::kExternal, true>,
            std::decay_t<R>> ||
        std::is_base_of_v<
            RenderGraphResource<RenderGraphResourceType::kBuffer, RenderGraphResourceState::kExternal, true>,
            std::decay_t<R>>;
    inline static constexpr bool IsManagedImageAlias = std::is_base_of_v<
        RenderGraphResource<RenderGraphResourceType::kImage, RenderGraphResourceState::kManaged, true>,
        std::decay_t<R>>;
}; */

struct RenderGraphSamplerInfo {
	VkFilter filter;
	VkSamplerMipmapMode mipmap_mode;
	VkSamplerAddressMode address_mode;
	inline bool operator<(const RenderGraphSamplerInfo &r) const {
		return std::tie(filter, mipmap_mode, address_mode) < std::tie(r.filter, r.mipmap_mode, r.address_mode);
	}
};
struct RenderGraphInputDescriptorInfo {
	VkShaderStageFlags stage_flags;
	RenderGraphSamplerInfo sampler_info;
	Ptr<Sampler> sampler;
};
class RenderGraphInput {
private:
	const RenderGraphResourceBase *m_resource_ptr;
	RenderGraphInputUsage m_usage;
	RenderGraphInputDescriptorInfo m_descriptor_info;

	friend class RenderGraphDescriptorPassBase;

public:
	inline RenderGraphInput(const RenderGraphResourceBase *resource_ptr, RenderGraphInputUsage usage)
	    : m_resource_ptr{resource_ptr}, m_usage{usage}, m_descriptor_info{} {}
	inline RenderGraphInput(const RenderGraphResourceBase *resource_ptr, RenderGraphInputUsage usage,
	                        RenderGraphInputDescriptorInfo descriptor_info)
	    : m_resource_ptr{resource_ptr}, m_usage{usage}, m_descriptor_info{std::move(descriptor_info)} {}

	inline const RenderGraphResourceBase *GetResource() const { return m_resource_ptr; }
	inline RenderGraphInputUsage GetUsage() const { return m_usage; }
	inline const RenderGraphInputDescriptorInfo &GetDescriptorInfo() const { return m_descriptor_info; }
};

enum class RenderGraphPassType { kGraphics, kCompute, kGroup, kCustom };
class RenderGraphPassBase : public RenderGraphObjectBase {
public:
	using OwnerPtr = std::variant<RenderGraphBase *, const RenderGraphPassBase *>;

private:
	OwnerPtr m_owner_ptr;

protected:
	/* template <typename R, typename = std::enable_if_t<RenderGraphResourceTraits<R>::IsResource>>
	inline auto MakeOutput(const Ptr<R> &resource) const {
	    constexpr auto State = RenderGraphResourceTraits<R>::State;
	    constexpr auto Type = RenderGraphResourceTraits<R>::Type;
	    if constexpr (State != RenderGraphResourceState::NONE) {
	        return std::make_shared<RenderGraphResource<Type, State, true>>(GetSelfPtr<RenderGraphPassBase>(),
	                                                                        resource);
	    } else {
	        Ptr<RenderGraphResource<Type>> ret;
	        if (resource->GetState() == RenderGraphResourceState::kExternal) {
	            if (resource->IsAlias())
	                ret = std::make_shared<RenderGraphResource<Type, RenderGraphResourceState::kExternal, true>>(
	                    std::dynamic_pointer_cast<RenderGraphResource<Type, RenderGraphResourceState::kExternal, true>>(
	                        GetSelfPtr<RenderGraphPassBase>(), resource));
	            else
	                ret = std::make_shared<RenderGraphResource<Type, RenderGraphResourceState::kExternal, true>>(
	                    std::dynamic_pointer_cast<
	                        RenderGraphResource<Type, RenderGraphResourceState::kExternal, false>>(
	                        GetSelfPtr<RenderGraphPassBase>(), resource));
	        } else if (resource->GetState() == RenderGraphResourceState::kManaged) {
	            if (resource->IsAlias())
	                ret = std::make_shared<RenderGraphResource<Type, RenderGraphResourceState::kExternal, true>>(
	                    std::dynamic_pointer_cast<RenderGraphResource<Type, RenderGraphResourceState::kManaged, true>>(
	                        GetSelfPtr<RenderGraphPassBase>(), resource));
	            else
	                ret = std::make_shared<RenderGraphResource<Type, RenderGraphResourceState::kExternal, true>>(
	                    std::dynamic_pointer_cast<RenderGraphResource<Type, RenderGraphResourceState::kManaged, false>>(
	                        GetSelfPtr<RenderGraphPassBase>(), resource));
	        }
	        return ret;
	    }
	}

	inline auto MakeOutput(
	    const std::vector<std::variant<
	        Ptr<const RenderGraphResource<RenderGraphResourceType::kImage, RenderGraphResourceState::kManaged, true>>,
	        Ptr<const RenderGraphResource<RenderGraphResourceType::kImage, RenderGraphResourceState::kManaged, false>>>>
	        &resources,
	    VkImageViewType image_view_type, VkImageAspectFlags image_aspects) const {
	    return std::make_shared<
	        RenderGraphResource<RenderGraphResourceType::kImage, RenderGraphResourceState::kManaged, true>>(
	        GetSelfPtr<RenderGraphPassBase>(), resources, image_view_type, image_aspects);
	} */

private:
	inline static constexpr uint32_t make_inputs_counter() { return 0; }
	template <typename... Args>
	inline static constexpr uint32_t make_inputs_counter(const RenderGraphInput &, Args... args) {
		return make_inputs_counter(args...) + 1u;
	}
	template <typename... Args>
	inline static constexpr uint32_t make_inputs_counter(const std::optional<RenderGraphInput> &optional_input,
	                                                     Args... args) {
		return make_inputs_counter(args...) + optional_input.has_value();
	}
	template <typename... Args>
	inline static constexpr uint32_t make_inputs_counter(const std::vector<RenderGraphInput> &vector, Args... args) {
		return make_inputs_counter(args...) + vector.size();
	}
	template <bool InputUsageClass(RenderGraphInputUsage), typename... Args>
	inline static constexpr uint32_t make_inputs_counter(const RenderGraphInputGroup<InputUsageClass> &group,
	                                                     Args... args) {
		return make_inputs_counter(args...) + group.GetSize();
	}

	inline static void make_inputs_filler(RenderGraphInput *begin) {}
	template <typename... Args>
	inline static void make_inputs_filler(RenderGraphInput *begin, const RenderGraphInput &input, Args... args) {
		*begin = input;
		make_inputs_filler(begin + 1, args...);
	}
	template <typename... Args>
	inline static void make_inputs_filler(RenderGraphInput *begin,
	                                      const std::optional<RenderGraphInput> &optional_input, Args... args) {
		if (optional_input.has_value()) {
			*begin = optional_input.value();
			make_inputs_filler(begin + 1, args...);
		} else
			make_inputs_filler(begin, args...);
	}
	template <typename... Args>
	inline static void make_inputs_counter(RenderGraphInput *begin, const std::vector<RenderGraphInput> &vector,
	                                       Args... args) {
		std::copy(vector.begin(), vector.end(), begin);
		make_inputs_filler(begin + vector.size(), args...);
	}
	template <bool InputUsageClass(RenderGraphInputUsage), typename... Args>
	inline static void make_inputs_counter(RenderGraphInput *begin, const RenderGraphInputGroup<InputUsageClass> &group,
	                                       Args... args) {
		std::copy(group.GetInputs().begin(), group.GetInputs().end(), begin);
		make_inputs_filler(begin + group.GetSize(), args...);
	}

protected:
	// TODO: Optimize MakeInputs() and GetInputs() interface
	template <typename... Args> inline std::vector<RenderGraphInput> MakeInputs(Args... args) {
		uint32_t count{make_inputs_counter(args...)};
		std::vector<RenderGraphInput> ret(count);
		make_inputs_filler(ret.data(), args...);
		return ret;
	}

public:
	inline explicit RenderGraphPassBase(const OwnerPtr &owner)
	    : m_owner_ptr{owner},
	      RenderGraphObjectBase(owner.index() ? std::get<1>(owner)->GetRenderGraphPtr() : std::get<0>(owner)) {}
	/* inline explicit RenderGraphPassBase(const std::weak_ptr<const RenderGraph> &owner)
	    : m_owner_weak_ptr{owner}, m_render_graph_weak_ptr{owner} {}
	inline explicit RenderGraphPassBase(const Ptr<const RenderGraphPassBase> &owner)
	    : m_owner_weak_ptr{owner}, m_render_graph_weak_ptr{owner->m_render_graph_weak_ptr} {}
	inline explicit RenderGraphPassBase(const std::weak_ptr<const RenderGraphPassBase> &owner)
	    : m_owner_weak_ptr{owner}, m_render_graph_weak_ptr{owner.lock()->m_render_graph_weak_ptr} {} */
	~RenderGraphPassBase() override = default;

	virtual RenderGraphPassType GetType() const = 0;
	virtual bool UseDescriptors() const { return false; }
	virtual void CmdRun(const Ptr<CommandBuffer> &command_buffer, uint32_t frame) = 0;
	virtual std::vector<RenderGraphInput> GetInputs() const = 0;

	inline bool IsSecondary() const { return m_owner_ptr.index(); }
	inline const RenderGraphPassBase *GetParentPassPtr() const {
		return IsSecondary() ? std::get<1>(m_owner_ptr) : nullptr;
	}
};

// Resources
class RenderGraphBufferBase : public RenderGraphResourceBase {
public:
	inline explicit RenderGraphBufferBase(const RenderGraphPassBase *producer_pass_ptr)
	    : RenderGraphResourceBase(producer_pass_ptr) {}
	inline explicit RenderGraphBufferBase(RenderGraphBase *render_graph_ptr)
	    : RenderGraphResourceBase(render_graph_ptr) {}
	~RenderGraphBufferBase() override = default;

	inline RenderGraphResourceType GetType() const final { return RenderGraphResourceType::kBuffer; }
	inline bool IsAlias() const override { return false; }
	virtual const Ptr<BufferBase> &GetBuffer(uint32_t frame = 0) const = 0;
};

enum class RenderGraphAttachmentLoadOp { kClear, kLoad, kDontCare };
class RenderGraphImageBase : public RenderGraphResourceBase {
public:
	inline explicit RenderGraphImageBase(const RenderGraphPassBase *producer_pass_ptr)
	    : RenderGraphResourceBase(producer_pass_ptr) {}
	inline explicit RenderGraphImageBase(RenderGraphBase *render_graph_ptr)
	    : RenderGraphResourceBase(render_graph_ptr) {}
	~RenderGraphImageBase() override = default;

	inline RenderGraphResourceType GetType() const final { return RenderGraphResourceType::kImage; }
	inline bool IsAlias() const override { return false; }
	virtual const Ptr<ImageView> &GetImageView(uint32_t frame = 0) const = 0;

	virtual RenderGraphAttachmentLoadOp GetLoadOp() const = 0;
	virtual const VkClearValue &GetClearValue() const = 0;
};

class RenderGraphExternalBufferBase : public RenderGraphBufferBase {
public:
	inline explicit RenderGraphExternalBufferBase(RenderGraphBase *render_graph_ptr)
	    : RenderGraphBufferBase(render_graph_ptr) {}
	~RenderGraphExternalBufferBase() override = default;

	inline RenderGraphResourceState GetState() const final { return RenderGraphResourceState::kExternal; }
};

class RenderGraphExternalImageBase : public RenderGraphImageBase {
public:
	inline explicit RenderGraphExternalImageBase(RenderGraphBase *render_graph_ptr)
	    : RenderGraphImageBase(render_graph_ptr) {}
	~RenderGraphExternalImageBase() override = default;

	inline RenderGraphResourceState GetState() const final { return RenderGraphResourceState::kExternal; }

private:
	RenderGraphAttachmentLoadOp m_load_op{RenderGraphAttachmentLoadOp::kDontCare};
	VkClearValue m_clear_value{};

public:
	template <RenderGraphAttachmentLoadOp LoadOp,
	          typename = std::enable_if_t<LoadOp != RenderGraphAttachmentLoadOp::kClear>>
	inline void SetLoadOp() {
		m_load_op = LoadOp;
	}
	template <RenderGraphAttachmentLoadOp LoadOp,
	          typename = std::enable_if_t<LoadOp == RenderGraphAttachmentLoadOp::kClear>>
	inline void SetLoadOp(const VkClearValue &clear_value) {
		m_load_op = LoadOp;
		m_clear_value = clear_value;
	}

	inline RenderGraphAttachmentLoadOp GetLoadOp() const final { return m_load_op; }
	inline const VkClearValue &GetClearValue() const final { return m_clear_value; }
};

#ifdef MYVK_ENABLE_GLFW
class RenderGraphSwapchainImage final : public RenderGraphExternalImageBase {
private:
	Ptr<FrameManager> m_frame_manager;

public:
	inline explicit RenderGraphSwapchainImage(RenderGraphBase *render_graph_ptr, Ptr<FrameManager> frame_manager)
	    : RenderGraphExternalImageBase(render_graph_ptr), m_frame_manager{std::move(frame_manager)} {}
	~RenderGraphSwapchainImage() final = default;

	/* inline static Ptr<RenderGraphSwapchainImage> Create(const Ptr<FrameManager> &frame_manager) {
	    return std::make_shared<RenderGraphSwapchainImage>(frame_manager);
	} */

	// inline bool IsPerFrame() const final { return true; }

	inline const Ptr<ImageView> &GetImageView(uint32_t frame = 0) const final {
		return m_frame_manager->GetCurrentSwapchainImageView();
	}
};
#endif

namespace _render_graph_resource_managed {

class RenderGraphManagedBuffer final : public RenderGraphBufferBase {
public:
	using SizeFunc = std::function<uint32_t(uint32_t, uint32_t)>;

private:
	uint32_t m_size;
	SizeFunc m_size_func;

	std::vector<Ptr<BufferBase>> m_buffers;

public:
	inline RenderGraphResourceState GetState() const final { return RenderGraphResourceState::kManaged; }
	// inline bool IsPerFrame() const final { return true; }

	inline explicit RenderGraphManagedBuffer(RenderGraphBase *render_graph_ptr, uint32_t size)
	    : RenderGraphBufferBase(render_graph_ptr), m_size{size} {}
	template <class SizeFunc>
	inline explicit RenderGraphManagedBuffer(RenderGraphBase *render_graph_ptr, SizeFunc func)
	    : RenderGraphBufferBase(render_graph_ptr), m_size_func{func}, m_size{0} {}
	inline ~RenderGraphManagedBuffer() final = default;

	const Ptr<BufferBase> &GetBuffer(uint32_t frame = 0) const final {
		return m_buffers.size() == 1 ? m_buffers[0] : m_buffers[frame];
	}

	friend class RenderGraphBase;
};

} // namespace _render_graph_resource_managed

template <RenderGraphResourceState State> class RenderGraphBufferAlias : public RenderGraphBufferBase {
private:
	Ptr<const RenderGraphBufferBase> m_resource;

public:
	template <typename R, typename = std::enable_if_t<RenderGraphResourceTraits<R>::IsResource &&
	                                                  RenderGraphResourceTraits<R>::IsBuffer &&
	                                                  RenderGraphResourceTraits<R>::State == State>>
	inline explicit RenderGraphResource(std::weak_ptr<const RenderGraphPassBase> producer, const Ptr<R> &resource)
	    : RenderGraphResourceBase(std::move(producer)), m_resource{IsAlias ? resource->GetResource() : resource} {}
	~RenderGraphResource() final = default;

	template <typename R, typename = std::enable_if_t<RenderGraphResourceTraits<R>::IsResource &&
	                                                  RenderGraphResourceTraits<R>::IsBuffer &&
	                                                  RenderGraphResourceTraits<R>::State == State>>
	inline static Ptr<RenderGraphResource> Create(const Ptr<const RenderGraphPassBase> &producer,
	                                              const Ptr<R> &resource) {
		return std::make_shared<RenderGraphResource>(producer, resource);
	}

	inline const Ptr<const RenderGraphBufferBase> &GetResource() const { return m_resource; }

	inline RenderGraphResourceState GetState() const final { return State; }
	inline bool IsAlias() const final { return true; }

	// inline bool IsPerFrame() const final { return GetResource()->IsPerFrame(); }

	inline const Ptr<BufferBase> &GetBuffer(uint32_t frame = 0) const final { return GetResource()->GetBuffer(); }
};

// Image

// TODO: Use this
/* class RenderGraphAttachmentBase {
private:
    RenderGraphAttachmentLoadOp m_load_op{RenderGraphAttachmentLoadOp::kDontCare};
    VkClearValue m_clear_value{};

public:
    template <RenderGraphAttachmentLoadOp LoadOp,
              typename = std::enable_if_t<LoadOp != RenderGraphAttachmentLoadOp::kClear>>
    inline void SetLoadOp() {
        m_load_op = LoadOp;
    }
    template <RenderGraphAttachmentLoadOp LoadOp,
              typename = std::enable_if_t<LoadOp == RenderGraphAttachmentLoadOp::kClear>>
    inline void SetLoadOp(const VkClearValue &clear_value) {
        m_load_op = LoadOp;
        m_clear_value = clear_value;
    }

    inline RenderGraphAttachmentLoadOp GetLoadOp() const { return m_load_op; }
    inline const VkClearValue &GetClearValue() const { return m_clear_value; }
}; */

using RenderGraphManagedImage =
    RenderGraphResource<RenderGraphResourceType::kImage, RenderGraphResourceState::kManaged>;
template <>
class RenderGraphResource<RenderGraphResourceType::kImage, RenderGraphResourceState::kManaged> final
    : public RenderGraphImageBase {
public:
	struct SizeInfo {
		VkExtent3D size{};
		uint32_t base_mip_level{0}, level_count{1}, layer_count{1};
	};
	using SizeFunc = std::function<SizeInfo(uint32_t, uint32_t)>;
	inline static SizeInfo DefaultSize2D(uint32_t width, uint32_t height) { return {{width, height, 1u}, 0, 1, 1}; }

private:
	std::vector<Ptr<ImageView>> m_image_views;

	SizeInfo m_size;
	SizeFunc m_size_func;
	VkFormat m_format;

	VkImageViewType m_image_view_type;
	VkImageAspectFlags m_image_aspects;

	inline VkImageType get_image_type() {
		switch (m_image_view_type) {
		case VK_IMAGE_VIEW_TYPE_1D:
		case VK_IMAGE_VIEW_TYPE_1D_ARRAY:
			return VK_IMAGE_TYPE_1D;
		case VK_IMAGE_VIEW_TYPE_2D:
		case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
		case VK_IMAGE_VIEW_TYPE_CUBE:
		case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:
			return VK_IMAGE_TYPE_2D;
		case VK_IMAGE_VIEW_TYPE_3D:
			return VK_IMAGE_TYPE_3D;
		default:
			return VK_IMAGE_TYPE_2D;
		}
	}

public:
	inline RenderGraphResourceState GetState() const final { return RenderGraphResourceState::kManaged; }
	// inline bool IsPerFrame() const final { return true; }

	inline explicit RenderGraphResource(const SizeInfo &size, VkFormat format, VkImageViewType image_view_type,
	                                    VkImageAspectFlags image_aspects)
	    : m_size_func{nullptr}, m_size{size}, m_format{format}, m_image_view_type{image_view_type},
	      m_image_aspects{image_aspects} {}
	template <class SizeFunc>
	inline explicit RenderGraphResource(SizeFunc func, VkFormat format, VkImageViewType image_view_type,
	                                    VkImageAspectFlags image_aspects)
	    : m_size_func{func}, m_size{}, m_format{format}, m_image_view_type{image_view_type}, m_image_aspects{
	                                                                                             image_aspects} {}
	~RenderGraphResource() final = default;

	inline static Ptr<RenderGraphManagedImage>
	Create(const SizeInfo &size, VkFormat format, VkImageViewType image_view_type, VkImageAspectFlags image_aspects) {
		return std::make_shared<RenderGraphManagedImage>(size, format, image_view_type, image_aspects);
	}
	template <class SizeFunc>
	inline static Ptr<RenderGraphManagedImage>
	Create(SizeFunc size_func, VkFormat format, VkImageViewType image_view_type, VkImageAspectFlags image_aspects) {
		return std::make_shared<RenderGraphManagedImage>(size_func, format, image_view_type, image_aspects);
	}
	inline static Ptr<RenderGraphManagedImage>
	CreateScreen2D(VkFormat format, VkImageAspectFlags image_aspects = VK_IMAGE_ASPECT_COLOR_BIT) {
		return Create(DefaultSize2D, format, VK_IMAGE_VIEW_TYPE_2D, image_aspects);
	}
	inline static Ptr<RenderGraphManagedImage> CreateDepth2D() {
		auto ret = CreateScreen2D(VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT);
		VkClearValue clear_value;
		clear_value.depthStencil = {1.0f, 0u};
		ret->SetLoadOp<RenderGraphAttachmentLoadOp::kClear>(clear_value);
		return ret;
	}

	void Resize(const SizeInfo &size) { m_size = size; }

	inline const Ptr<ImageView> &GetImageView(uint32_t frame = 0) const final {
		return m_image_views.size() == 1 ? m_image_views[0] : m_image_views[frame];
	}

	inline VkImageViewType GetImageViewType() const { return m_image_view_type; }
	inline VkImageAspectFlags GetImageAspects() const { return m_image_aspects; }

private:
	RenderGraphAttachmentLoadOp m_load_op{RenderGraphAttachmentLoadOp::kDontCare};
	VkClearValue m_clear_value{};

public:
	template <RenderGraphAttachmentLoadOp LoadOp,
	          typename = std::enable_if_t<LoadOp != RenderGraphAttachmentLoadOp::kClear>>
	inline void SetLoadOp() {
		m_load_op = LoadOp;
	}
	template <RenderGraphAttachmentLoadOp LoadOp,
	          typename = std::enable_if_t<LoadOp == RenderGraphAttachmentLoadOp::kClear>>
	inline void SetLoadOp(const VkClearValue &clear_value) {
		m_load_op = LoadOp;
		m_clear_value = clear_value;
	}

	inline RenderGraphAttachmentLoadOp GetLoadOp() const final { return m_load_op; }
	inline const VkClearValue &GetClearValue() const final { return m_clear_value; }

	friend class RenderGraphBase;
};

/* template <RenderGraphInputUsage StaticUsage>
using RenderGraphStaticImage =
    RenderGraphResource<RenderGraphResourceType::kImage, RenderGraphResourceState::kStatic, false, StaticUsage>;

template <RenderGraphInputUsage StaticUsage>
class RenderGraphResource<RenderGraphResourceType::kImage, RenderGraphResourceState::kStatic, false, StaticUsage> final
    : public RenderGraphImageBase {
    static_assert(RenderGraphInputUsageIsReadOnly(StaticUsage) && RenderGraphInputUsageForImage(StaticUsage));

private:
    Ptr<ImageView> m_buffer;

public:
    explicit RenderGraphResource(Ptr<ImageView> image_view) : m_buffer{std::move(image_view)} {}
    ~RenderGraphResource() final = default;

    inline RenderGraphResourceState GetState() const final { return RenderGraphResourceState::kStatic; }
    // inline bool IsPerFrame() const final { return false; }

    inline static Ptr<RenderGraphResource> Create(const Ptr<ImageView> &buffer) {
        return std::make_shared<RenderGraphResource>(buffer);
    }

    inline const Ptr<ImageView> &GetImageView(uint32_t frame = 0) const final { return m_buffer; }
    inline RenderGraphInputUsage GetUsage() const { return StaticUsage; }
}; */

template <RenderGraphResourceState State>
using RenderGraphImageAlias = RenderGraphResource<RenderGraphResourceType::kImage, State, true>;

template <RenderGraphResourceState State>
class RenderGraphResource<RenderGraphResourceType::kImage, State, true> final : public RenderGraphImageBase {
private:
	Ptr<const RenderGraphImageBase> m_resource;

public:
	template <typename R, typename = std::enable_if_t<RenderGraphResourceTraits<R>::IsResource &&
	                                                  RenderGraphResourceTraits<R>::IsImage &&
	                                                  RenderGraphResourceTraits<R>::State == State>>
	inline explicit RenderGraphResource(std::weak_ptr<const RenderGraphPassBase> producer, const Ptr<R> &resource)
	    : RenderGraphResourceBase(std::move(producer)), m_resource{IsAlias ? resource->GetResource() : resource} {}
	~RenderGraphResource() final = default;

	template <typename R, typename = std::enable_if_t<RenderGraphResourceTraits<R>::IsResource &&
	                                                  RenderGraphResourceTraits<R>::IsImage &&
	                                                  RenderGraphResourceTraits<R>::State == State>>
	inline static Ptr<RenderGraphResource> Create(const Ptr<RenderGraphPassBase> &producer, const Ptr<R> &resource) {
		return std::make_shared<RenderGraphResource>(producer, resource);
	}

	inline const Ptr<const RenderGraphImageBase> &GetResource() const { return m_resource; }

	inline RenderGraphResourceState GetState() const final { return State; }
	inline bool IsAlias() const final { return true; }

	// inline bool IsPerFrame() const final { return GetResource()->IsPerFrame(); }

	inline const Ptr<ImageView> &GetImageView(uint32_t frame = 0) const final { return GetResource()->GetImageView(); }

	inline RenderGraphAttachmentLoadOp GetLoadOp() const final { return GetResource()->GetLoadOp(); }
	inline const VkClearValue &GetClearValue() const final { return GetResource()->GetClearValue(); }
};

using RenderGraphManagedImageAlias =
    RenderGraphResource<RenderGraphResourceType::kImage, RenderGraphResourceState::kManaged, true>;
template <>
class RenderGraphResource<RenderGraphResourceType::kImage, RenderGraphResourceState::kManaged, true> final
    : public RenderGraphImageBase {
private:
	std::vector<Ptr<const RenderGraphImageBase>> m_resources;
	std::vector<Ptr<ImageView>> m_image_views;

	VkImageViewType m_image_view_type;
	VkImageAspectFlags m_image_aspects;

public:
	template <typename R, typename = std::enable_if_t<RenderGraphResourceTraits<R>::IsResource &&
	                                                  RenderGraphResourceTraits<R>::IsImage &&
	                                                  RenderGraphResourceTraits<R>::IsManaged>>
	inline RenderGraphResource(std::weak_ptr<const RenderGraphPassBase> producer, const Ptr<R> &resource)
	    : RenderGraphResourceBase(std::move(producer)), m_resources{resource->IsAlias() ? resource->GetResources()
	                                                                                    : resource},
	      m_image_view_type{resource->GetImageViewType()}, m_image_aspects{resource->GetImageAspects()} {}

	template <typename R, typename = std::enable_if_t<RenderGraphResourceTraits<R>::IsResource &&
	                                                  RenderGraphResourceTraits<R>::IsImage &&
	                                                  RenderGraphResourceTraits<R>::IsManaged>>
	inline RenderGraphResource(std::weak_ptr<const RenderGraphPassBase> producer, const Ptr<R> &resource,
	                           VkImageViewType image_view_type, VkImageAspectFlags image_aspects)
	    : RenderGraphResourceBase(std::move(producer)), m_resources{resource->IsAlias() ? resource->GetResources()
	                                                                                    : resource},
	      m_image_view_type{image_view_type}, m_image_aspects{image_aspects} {}

	inline RenderGraphResource(
	    std::weak_ptr<const RenderGraphPassBase> producer,
	    const std::vector<std::variant<
	        Ptr<const RenderGraphResource<RenderGraphResourceType::kImage, RenderGraphResourceState::kManaged, true>>,
	        Ptr<const RenderGraphResource<RenderGraphResourceType::kImage, RenderGraphResourceState::kManaged, false>>>>
	        &resources,
	    VkImageViewType image_view_type, VkImageAspectFlags image_aspects)
	    : RenderGraphResourceBase(std::move(producer)), m_image_view_type{image_view_type}, m_image_aspects{
	                                                                                            image_aspects} {
		m_resources.reserve(resources.size());
		for (const auto &r : resources) {
			if (std::holds_alternative<Ptr<const RenderGraphResource<RenderGraphResourceType::kImage,
			                                                         RenderGraphResourceState::kManaged, true>>>(r))
				m_resources.push_back(
				    std::get<Ptr<const RenderGraphResource<RenderGraphResourceType::kImage,
				                                           RenderGraphResourceState::kManaged, true>>>(r));
			else
				m_resources.push_back(
				    std::get<Ptr<const RenderGraphResource<RenderGraphResourceType::kImage,
				                                           RenderGraphResourceState::kManaged, false>>>(r));
		}
	}
	~RenderGraphResource() final = default;

	/* template <bool IsAlias>
	inline static Ptr<RenderGraphResource>
	Create(const Ptr<RenderGraphPassBase> &producer,
	       const Ptr<RenderGraphResource<RenderGraphResourceType::kImage,
	                                                 RenderGraphResourceState::kManaged, IsAlias>> &resource,
	       VkImageViewType image_view_type, VkImageAspectFlags image_aspects) {
	    return std::make_shared<RenderGraphResource>(producer, resource, image_view_type, image_aspects);
	} */

	inline static Ptr<RenderGraphResource> Create(
	    // const Ptr<const RenderGraphPassBase> &producer,
	    const std::vector<std::variant<
	        Ptr<const RenderGraphResource<RenderGraphResourceType::kImage, RenderGraphResourceState::kManaged, true>>,
	        Ptr<const RenderGraphResource<RenderGraphResourceType::kImage, RenderGraphResourceState::kManaged, false>>>>
	        &resources,
	    VkImageViewType image_view_type, VkImageAspectFlags image_aspects) {
		return std::make_shared<RenderGraphResource>(Ptr<const RenderGraphPassBase>{nullptr}, resources,
		                                             image_view_type, image_aspects);
	}

	inline const std::vector<Ptr<const RenderGraphImageBase>> &GetResources() const { return m_resources; }

	inline RenderGraphResourceState GetState() const final { return RenderGraphResourceState::kManaged; }
	inline bool IsAlias() const final { return true; }

	// inline bool IsPerFrame() const final { return GetResource()->IsPerFrame(); }

	inline const Ptr<ImageView> &GetImageView(uint32_t frame = 0) const final {
		return m_image_views.size() == 1 ? m_image_views[0] : m_image_views[frame];
	}

	inline VkImageViewType GetImageViewType() const { return m_image_view_type; }
	inline VkImageAspectFlags GetImageAspects() const { return m_image_aspects; }

	inline RenderGraphAttachmentLoadOp GetLoadOp() const final {
		return m_resources.size() == 1 ? GetResources()[0]->GetLoadOp() : RenderGraphAttachmentLoadOp::kDontCare;
	}
	inline const VkClearValue &GetClearValue() const final { return GetResources()[0]->GetClearValue(); }

	friend class RenderGraphBase;
};

inline constexpr bool RenderGraphInputGroupDefaultClass(RenderGraphInputUsage) { return true; }
template <bool InputUsageClass(RenderGraphInputUsage)> class RenderGraphInputGroup {
private:
	std::map<std::string, uint32_t> m_input_map;
	std::vector<RenderGraphInput> m_inputs;

public:
	inline RenderGraphInputGroup() = default;
	inline RenderGraphInputGroup(const RenderGraphInputGroup &) noexcept = delete;
	inline RenderGraphInputGroup(RenderGraphInputGroup &&) noexcept = default;
	inline virtual ~RenderGraphInputGroup() = default;

	inline RenderGraphInputGroup &Clear() {
		m_input_map.clear();
		m_inputs.clear();
		return *this;
	}
	inline const std::vector<RenderGraphInput> &GetInputs() const { return m_inputs; }
	inline uint32_t GetSize() const { return m_inputs.size(); }

	template <RenderGraphInputUsage Usage, typename R,
	          typename = std::enable_if_t<RenderGraphResourceTraits<R>::IsResource &&
	                                      RenderGraphInputUsageForType<RenderGraphResourceTraits<R>::Type>(Usage) &&
	                                      !RenderGraphInputUsageIsDescriptor(Usage)>>
	inline RenderGraphInputGroup &PushBack(const std::string &name, const Ptr<R> &resource) {
		assert(m_input_map.find(name) == m_input_map.end());
		m_input_map[name] = m_inputs.size();
		m_inputs.push_back({resource, Usage});
		return *this;
	}

	template <RenderGraphInputUsage Usage, typename R,
	          typename = std::enable_if_t<RenderGraphResourceTraits<R>::IsResource &&
	                                      RenderGraphInputUsageForType<RenderGraphResourceTraits<R>::Type>(Usage) &&
	                                      RenderGraphInputUsageIsDescriptor(Usage) &&
	                                      Usage != RenderGraphInputUsage::kSampledImage>>
	inline RenderGraphInputGroup &PushBack(const std::string &name, const Ptr<R> &resource,
	                                       VkShaderStageFlags stage_flags) {
		assert(m_input_map.find(name) == m_input_map.end());
		m_input_map[name] = m_inputs.size();
		RenderGraphInput input{resource, Usage};
		m_inputs.push_back({resource, Usage, {stage_flags}});
		return *this;
	}

	template <RenderGraphInputUsage Usage, typename R,
	          typename = std::enable_if_t<RenderGraphResourceTraits<R>::IsResource &&
	                                      RenderGraphInputUsageForType<RenderGraphResourceTraits<R>::Type>(Usage) &&
	                                      Usage == RenderGraphInputUsage::kSampledImage>>
	inline RenderGraphInputGroup &PushBack(const std::string &name, const Ptr<R> &resource,
	                                       VkShaderStageFlags stage_flags, const RenderGraphSamplerInfo &sampler_info) {
		assert(m_input_map.find(name) == m_input_map.end() && sampler_info != nullptr);
		m_input_map[name] = m_inputs.size();
		m_inputs.push_back({resource, Usage, {stage_flags, sampler_info}});
		return *this;
	}

	template <RenderGraphInputUsage Usage, typename R,
	          typename = std::enable_if_t<RenderGraphResourceTraits<R>::IsResource &&
	                                      RenderGraphInputUsageForType<RenderGraphResourceTraits<R>::Type>(Usage) &&
	                                      Usage == RenderGraphInputUsage::kSampledImage>>
	inline RenderGraphInputGroup &PushBack(const std::string &name, const Ptr<R> &resource,
	                                       VkShaderStageFlags stage_flags, const Ptr<Sampler> &sampler) {
		assert(m_input_map.find(name) == m_input_map.end());
		m_input_map[name] = m_inputs.size();
		m_inputs.push_back({resource, Usage, {stage_flags, 0, sampler}});
		return *this;
	}

	inline RenderGraphInputGroup &Erase(const std::string &name) {
		uint32_t idx;
		{
			auto it = m_input_map.find(name);
			assert(it != m_input_map.end());
			idx = it->second;
			m_inputs.erase(m_inputs.begin() + it->second);
			m_input_map.erase(it);
		}
		for (auto &it : m_input_map) {
			if (it.second > idx)
				--it.second;
		}
	}
	/* template <RenderGraphInputUsage StaticUsage, typename = std::enable_if_t<InputUsageClass(StaticUsage)>>
	inline RenderGraphInputGroup &PushBack(std::string_view name,
	                                       const Ptr<const RenderGraphStaticImage<StaticUsage>> &resource) {
	    push_back(name, {resource, StaticUsage});
	    return *this;
	} */

	RenderGraphInput &operator[](const std::string &name) { return m_inputs[m_input_map.at(name)]; }
	const RenderGraphInput &operator[](const std::string &name) const { return m_inputs[m_input_map.at(name)]; }

	RenderGraphInput &operator[](uint32_t idx) { return m_inputs[idx]; }
	const RenderGraphInput &operator[](uint32_t idx) const { return m_inputs[idx]; }
};

class RenderGraphInfo {
private:
	std::weak_ptr<const RenderGraphBase> m_owner;

	std::map<std::string, Ptr<RenderGraphPassBase>> m_passes;
	std::map<std::string, RenderGraphInput> m_outputs;

public:
	inline explicit RenderGraphInfo(std::weak_ptr<const RenderGraphBase> owner) : m_owner{std::move(owner)} {}

	template <typename PassT, typename... Args,
	          typename = std::enable_if_t<std::is_base_of_v<RenderGraphPassBase, PassT>>>
	inline PassT &AddPass(const std::string &name, Args &&...args) {
		assert(m_passes.find(name) == m_passes.end());
		auto ptr = std::make_shared<PassT>(m_owner, std::forward<Args>(args)...);
		PassT &ret = *ptr;
		m_passes.insert({name, std::move(ptr)});
		return ret;
	}

	template <RenderGraphInputUsage Usage, typename R,
	          typename = std::enable_if_t<RenderGraphResourceTraits<R>::IsResource &&
	                                      RenderGraphInputUsageForType<RenderGraphResourceTraits<R>::Type>(Usage)>>
	inline void SetOutput(const std::string &name, const Ptr<R> &resource) {
		assert(m_outputs.find(name) == m_outputs.end());
		m_outputs.insert({name, myvk::RenderGraphInput(resource, Usage)});
	}

	friend class RenderGraphBase;
};

using RenderGraphBuilderFunc = std::function<void(RenderGraphInfo &)>;

class RenderGraphBase : public DeviceObjectBase {
private:
	Ptr<Device> m_device_ptr;

	std::map<std::string, Ptr<RenderGraphPassBase>> m_passes;
	std::map<std::string, RenderGraphInput> m_outputs;

	std::map<RenderGraphSamplerInfo, std::weak_ptr<Sampler>> m_sampler_cache;

	bool m_recompile_flag{true};

	Ptr<Sampler> get_sampler(const RenderGraphSamplerInfo &sampler_info);

	friend class RenderGraphDescriptorPassBase;
	template <typename Scheduler> friend class RenderGraph;

protected:
	template <typename Scheduler>
	inline void Compile(Scheduler &scheduler, const RenderGraphBuilderFunc &builder = nullptr) {
		if (builder) {
			RenderGraphInfo info{GetSelfPtr<RenderGraphBase>()};
			builder(info);
			m_passes = std::move(info.m_passes);
			m_outputs = std::move(info.m_outputs);
		}
	}

public:
	inline explicit RenderGraphBase(Ptr<Device> device_ptr) : m_device_ptr{std::move(device_ptr)} {}
	inline ~RenderGraphBase() override = default;

	template <typename PassT = RenderGraphPassBase,
	          typename = std::enable_if_t<std::is_base_of_v<RenderGraphPassBase, PassT>>>
	inline PassT &GetPass(const std::string &name) {
		return *std::dynamic_pointer_cast<PassT>(m_passes.at(name));
	}
	template <typename PassT = RenderGraphPassBase,
	          typename = std::enable_if_t<std::is_base_of_v<RenderGraphPassBase, PassT>>>
	inline const PassT &GetPass(const std::string &name) const {
		return *std::dynamic_pointer_cast<const PassT>(m_passes.at(name));
	}
	inline const RenderGraphInput &GetOutput(const std::string &name) const { return m_outputs.at(name); }

	inline void SetRecompile() { m_recompile_flag = true; }
	inline const Ptr<Device> &GetDevicePtr() const final { return m_device_ptr; }
};

template <typename Scheduler = RenderGraphDefaultScheduler> class RenderGraph final : public RenderGraphBase {
private:
	Scheduler m_scheduler{};

public:
	inline explicit RenderGraph(Ptr<Device> device_ptr) : RenderGraphBase{std::move(device_ptr)} {}
	inline ~RenderGraph() final = default;

	inline static Ptr<RenderGraph> Create(const Ptr<Device> &device_ptr, const RenderGraphBuilderFunc &builder) {
		auto ret = std::make_shared<RenderGraph>(device_ptr);
		ret->Compile(ret->m_scheduler, builder);
		return ret;
	}

	void CmdRun(const Ptr<CommandBuffer> &command_buffer, uint32_t frame);
};

class RenderGraphDescriptorPassBase : public RenderGraphPassBase {
private:
	RenderGraphInputGroup<RenderGraphInputUsageIsDescriptor> m_descriptor_group;

	Ptr<DescriptorSetLayout> m_descriptor_layout;
	std::vector<Ptr<DescriptorSet>> m_descriptor_sets;

	void create_descriptors_layout();

public:
	inline explicit RenderGraphDescriptorPassBase(
	    const OwnerWeakPtr &owner, RenderGraphInputGroup<RenderGraphInputUsageIsDescriptor> &&descriptor_group)
	    : RenderGraphPassBase(owner), m_descriptor_group{std::move(descriptor_group)} {
		create_descriptors_layout();
	}

	inline const auto &GetDescriptorGroup() const { return m_descriptor_group; }
	// auto &GetDescriptorGroup() { return m_descriptor_group; }

	inline const auto &GetDescriptorLayout() const { return m_descriptor_layout; }
	inline const auto &GetDescriptorSet(uint32_t frame) const {
		return m_descriptor_sets.size() == 1 ? m_descriptor_sets.front() : m_descriptor_sets[frame];
	}

	bool UseDescriptors() const final { return true; }
};

class RenderGraphGraphicsPassBase : virtual public RenderGraphPassBase {
public:
	inline RenderGraphPassType GetType() const final { return RenderGraphPassType::kGraphics; }
};
class RenderGraphComputePassBase : virtual public RenderGraphPassBase {
public:
	inline RenderGraphPassType GetType() const final { return RenderGraphPassType::kCompute; }
};

class RenderGraphPassGroupInfo {
private:
	std::weak_ptr<const RenderGraphPassBase> m_owner;
	std::vector<Ptr<RenderGraphPassBase>> m_passes;

public:
	inline explicit RenderGraphPassGroupInfo(std::weak_ptr<const RenderGraphPassBase> owner)
	    : m_owner{std::move(owner)} {}
	template <typename PassT, typename... Args,
	          typename = std::enable_if_t<std::is_base_of_v<RenderGraphPassBase, PassT>>>
	inline PassT &AddPass(Args &&...args) {
		m_passes.push_back(std::make_shared<PassT>(m_owner, std::forward<Args>(args)...));
		return m_passes.back();
	}

	friend class RenderGraphPassGroupBase;
};

class RenderGraphPassGroupBase : virtual public RenderGraphPassBase {
private:
	std::vector<Ptr<RenderGraphPassBase>> m_passes;

protected:
	inline void Update(const std::function<void(RenderGraphPassGroupInfo &)> &builder) {
		RenderGraphPassGroupInfo info{GetSelfPtr<RenderGraphPassBase>()};
		builder(info);
		m_passes = std::move(info.m_passes);

		LockRenderGraph()->SetRecompile();
	}
	template <typename PassT = RenderGraphPassBase,
	          typename = std::enable_if_t<std::is_base_of_v<RenderGraphPassBase, PassT>>>
	PassT &GetPass(uint32_t idx) {
		return *std::dynamic_pointer_cast<PassT>(m_passes[idx]);
	}
	template <typename PassT = RenderGraphPassBase,
	          typename = std::enable_if_t<std::is_base_of_v<RenderGraphPassBase, PassT>>>
	const PassT &GetPass(uint32_t idx) const {
		return *std::dynamic_pointer_cast<PassT>(m_passes[idx]);
	}

public:
	~RenderGraphPassGroupBase() override = default;
	RenderGraphPassType GetType() const final { return RenderGraphPassType::kGroup; }
};

} // namespace myvk

#endif
