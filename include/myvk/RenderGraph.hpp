#ifndef SCALABLERTGI_RENDERGRAPH_HPP
#define SCALABLERTGI_RENDERGRAPH_HPP

#include "CommandBuffer.hpp"
#include "FrameManager.hpp"
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
inline constexpr bool RenderGraphInputUsageDescriptorEnabled(RenderGraphInputUsage usage) {
	switch (usage) {
	case RenderGraphInputUsage::kInputAttachment:
	case RenderGraphInputUsage::kSampledImage:
	case RenderGraphInputUsage::kStorageImageR:
	case RenderGraphInputUsage::kStorageImageW:
	case RenderGraphInputUsage::kStorageImageRW:
	case RenderGraphInputUsage::kUniformBuffer:
	case RenderGraphInputUsage::kStorageBufferR:
	case RenderGraphInputUsage::kStorageBufferW:
	case RenderGraphInputUsage::kStorageBufferRW:
		return true;
	default:
		return false;
	}
}

inline constexpr bool RenderGraphInputUsageIsReadOnly(RenderGraphInputUsage usage) {
	switch (usage) {
		CASE(kPresent)          //
		CASE(kPreserve)         //
		CASE(kDepthAttachmentR) //
		CASE(kInputAttachment)  //
		CASE(kSampledImage)     //
		CASE(kStorageImageR)    //
		CASE(kUniformBuffer)    //
		CASE(kStorageBufferR)   //
		CASE(kIndexBuffer)      //
		CASE(kVertexBuffer)     //
		CASE(kTransferSrc)      //
		return true;
	default:
		return false;
	}
}

inline constexpr bool RenderGraphInputUsageForBuffer(RenderGraphInputUsage usage) {
	switch (usage) {
		CASE(kPreserve)        //
		CASE(kUniformBuffer)   //
		CASE(kStorageBufferR)  //
		CASE(kStorageBufferW)  //
		CASE(kStorageBufferRW) //
		CASE(kIndexBuffer)     //
		CASE(kVertexBuffer)    //
		CASE(kTransferSrc)     //
		CASE(kTransferDst)     //
		return true;
	default:
		return false;
	}
}

inline constexpr bool RenderGraphInputUsageForImage(RenderGraphInputUsage usage) {
	switch (usage) {
		CASE(kPreserve)          //
		CASE(kPresent)           //
		CASE(kColorAttachment)   //
		CASE(kDepthAttachmentR)  //
		CASE(kDepthAttachmentRW) //
		CASE(kInputAttachment)   //
		CASE(kSampledImage)      //
		CASE(kStorageImageR)     //
		CASE(kStorageImageW)     //
		CASE(kStorageImageRW)    //
		CASE(kTransferSrc)       //
		CASE(kTransferDst)       //
		return true;
	default:
		return false;
	}
}

#undef CASE

/* #define USAGE_TO_BASE(NAME) \
    RenderGraphInputUsage { (int)(NAME) }
#define USAGE_TO_READ_ONLY(NAME) \
    RenderGraphInputUsageReadOnly { (int)(NAME) }
#define ADD(NAME) NAME = (int)RenderGraphInputUsage::NAME,
enum class RenderGraphInputUsageReadOnly {
    ADD(kPresent)              //
    ADD(kPreserve)             //
    ADD(kDepthAttachmentLoadR) //
    ADD(kInputAttachment)      //
    ADD(kSampledImage)         //
    ADD(kStorageImageR)        //
    ADD(kUniformBuffer)        //
    ADD(kStorageBufferR)       //
    ADD(kIndexBuffer)          //
    ADD(kVertexBuffer)         //
    ADD(kTransferSrc)          //
};

enum class RenderGraphInputUsageClearAttachment {
    ADD(kColorAttachmentClear) //
    ADD(kDepthAttachmentClear) //
};

enum class RenderGraphInputUsageImage {
    ADD(kPreserve)                //
    ADD(kPresent)                 //
    ADD(kColorAttachmentClear)    //
    ADD(kColorAttachmentDontCare) //
    ADD(kColorAttachmentLoad)     //
    ADD(kDepthAttachmentClear)    //
    ADD(kDepthAttachmentLoadR)    //
    ADD(kDepthAttachmentLoadRW)   //
    ADD(kInputAttachment)         //
    ADD(kSampledImage)            //
    ADD(kStorageImageR)           //
    ADD(kStorageImageW)           //
    ADD(kStorageImageRW)          //
    ADD(kTransferSrc)             //
    ADD(kTransferDst)             //
};

enum class RenderGraphInputUsageImageReadOnly {
    ADD(kPreserve)             //
    ADD(kPresent)              //
    ADD(kDepthAttachmentLoadR) //
    ADD(kInputAttachment)      //
    ADD(kSampledImage)         //
    ADD(kStorageImageR)        //
    ADD(kTransferSrc)          //
};

enum class RenderGraphInputUsageBuffer {
    ADD(kPreserve)        //
    ADD(kUniformBuffer)   //
    ADD(kStorageBufferR)  //
    ADD(kStorageBufferW)  //
    ADD(kStorageBufferRW) //
    ADD(kIndexBuffer)     //
    ADD(kVertexBuffer)    //
    ADD(kTransferSrc)     //
    ADD(kTransferDst)     //
};

enum class RenderGraphInputUsageBufferReadOnly {
    ADD(kPreserve)       //
    ADD(kUniformBuffer)  //
    ADD(kStorageBufferR) //
    ADD(kIndexBuffer)    //
    ADD(kVertexBuffer)   //
    ADD(kTransferSrc)    //
};
#undef ADD */

class RenderGraph;

enum class RenderGraphResourceType { kImage, kBuffer };
enum class RenderGraphResourceState { NONE, kManaged, kExternal /*, kStatic*/ };

template <RenderGraphResourceType Type>
inline constexpr bool RenderGraphInputUsageForType(RenderGraphInputUsage usage) {
	if constexpr (Type == RenderGraphResourceType::kImage) {
		return RenderGraphInputUsageForImage(usage);
	} else {
		return RenderGraphInputUsageForBuffer(usage);
	}
}

class RenderGraphResourceBase : public Base {
private:
	std::weak_ptr<const RenderGraphPassBase> m_producer;

public:
	inline RenderGraphResourceBase() = default;
	inline explicit RenderGraphResourceBase(std::weak_ptr<const RenderGraphPassBase> producer)
	    : m_producer{std::move(producer)} {}

	virtual RenderGraphResourceType GetType() const = 0;
	virtual RenderGraphResourceState GetState() const = 0;
	virtual bool IsAlias() const = 0;
	// virtual bool IsPerFrame() const = 0;
	// virtual void Resize(uint32_t width, uint32_t height) {}

	inline Ptr<const RenderGraphPassBase> LockProducer() const { return m_producer.lock(); }

	virtual ~RenderGraphResourceBase() = default;
};

template <RenderGraphResourceType Type, RenderGraphResourceState State = RenderGraphResourceState::NONE,
          bool IsAlias = false /* , RenderGraphInputUsage StaticUsage = RenderGraphInputUsage::NONE*/>
class RenderGraphResource;

// Buffer
using RenderGraphBufferBase = RenderGraphResource<RenderGraphResourceType::kBuffer>;
template <> class RenderGraphResource<RenderGraphResourceType::kBuffer> : virtual public RenderGraphResourceBase {
public:
	~RenderGraphResource() override = default;
	inline RenderGraphResourceType GetType() const final { return RenderGraphResourceType::kBuffer; }
	inline bool IsAlias() const override { return false; }
	virtual const Ptr<BufferBase> &GetBuffer(uint32_t frame = 0) const = 0;
};

using RenderGraphManagedBuffer =
    RenderGraphResource<RenderGraphResourceType::kBuffer, RenderGraphResourceState::kManaged>;
template <>
class RenderGraphResource<RenderGraphResourceType::kBuffer, RenderGraphResourceState::kManaged> final
    : public RenderGraphBufferBase {
public:
	using SizeFunc = std::function<uint32_t(uint32_t, uint32_t)>;

private:
	uint32_t m_size;
	SizeFunc m_size_func;

	std::vector<Ptr<BufferBase>> m_buffers;

public:
	inline RenderGraphResourceState GetState() const final { return RenderGraphResourceState::kManaged; }
	// inline bool IsPerFrame() const final { return true; }

	inline explicit RenderGraphResource(uint32_t size) : m_size{size} {}
	template <class SizeFunc> inline explicit RenderGraphResource(SizeFunc func) : m_size_func{func}, m_size{0} {}
	~RenderGraphResource() final = default;

	inline static Ptr<RenderGraphManagedBuffer> Create(uint32_t size) {
		return std::make_shared<RenderGraphManagedBuffer>(size);
	}
	template <class SizeFunc> inline static Ptr<RenderGraphManagedBuffer> Create(SizeFunc &&size_func) {
		return std::make_shared<RenderGraphManagedBuffer>(std::forward(size_func));
	}

	const Ptr<BufferBase> &GetBuffer(uint32_t frame = 0) const final {
		return m_buffers.size() == 1 ? m_buffers[0] : m_buffers[frame];
	}

	friend class RenderGraph;
};

using RenderGraphExternalBufferBase =
    RenderGraphResource<RenderGraphResourceType::kBuffer, RenderGraphResourceState::kExternal>;
template <>
class RenderGraphResource<RenderGraphResourceType::kBuffer, RenderGraphResourceState::kExternal>
    : virtual public RenderGraphBufferBase {
public:
	~RenderGraphResource() override = default;

	inline RenderGraphResourceState GetState() const final { return RenderGraphResourceState::kExternal; }
};

/* template <RenderGraphInputUsage StaticUsage>
using RenderGraphStaticBuffer =
    RenderGraphResource<RenderGraphResourceType::kBuffer, RenderGraphResourceState::kStatic, false, StaticUsage>;

template <RenderGraphInputUsage StaticUsage>
class RenderGraphResource<RenderGraphResourceType::kBuffer, RenderGraphResourceState::kStatic, false, StaticUsage> final
    : public RenderGraphBufferBase {
    static_assert(RenderGraphInputUsageIsReadOnly(StaticUsage) && RenderGraphInputUsageForBuffer(StaticUsage));

private:
    Ptr<BufferBase> m_buffer;

public:
    explicit RenderGraphResource(Ptr<BufferBase> buffer) : m_buffer{std::move(buffer)} {}
    ~RenderGraphResource() final = default;

    inline RenderGraphResourceState GetState() const final { return RenderGraphResourceState::kStatic; }
    // inline bool IsPerFrame() const final { return false; }

    inline static Ptr<RenderGraphResource> Create(const Ptr<BufferBase> &buffer) {
        return std::make_shared<RenderGraphResource>(buffer);
    }

    inline const Ptr<BufferBase> &GetBuffer(uint32_t frame = 0) const final { return m_buffer; }
    inline RenderGraphInputUsage GetUsage() const { return StaticUsage; }
}; */

template <RenderGraphResourceState State>
using RenderGraphBufferAlias = RenderGraphResource<RenderGraphResourceType::kBuffer, State, true>;

template <RenderGraphResourceState State>
class RenderGraphResource<RenderGraphResourceType::kBuffer, State, true> final : public RenderGraphBufferBase {
private:
	Ptr<const RenderGraphBufferBase> m_resource;

public:
	template <bool IsAlias /* , typename = std::enable_if_t<State != RenderGraphResourceState::kStatic> */>
	inline explicit RenderGraphResource(
	    std::weak_ptr<const RenderGraphPassBase> producer,
	    const Ptr<const RenderGraphResource<RenderGraphResourceType::kBuffer, State, IsAlias>> &resource)
	    : RenderGraphResourceBase(std::move(producer)), m_resource{IsAlias ? resource->GetResource() : resource} {}
	~RenderGraphResource() final = default;

	template <bool IsAlias /* , typename = std::enable_if_t<State != RenderGraphResourceState::kStatic> */>
	inline static Ptr<RenderGraphResource>
	Create(const Ptr<const RenderGraphPassBase> &producer,
	       const Ptr<const RenderGraphResource<RenderGraphResourceType::kBuffer, State, IsAlias>> &resource) {
		return std::make_shared<RenderGraphResource>(producer, resource);
	}

	inline const Ptr<const RenderGraphBufferBase> &GetResource() const { return m_resource; }

	inline RenderGraphResourceState GetState() const final { return State; }
	inline bool IsAlias() const final { return true; }

	// inline bool IsPerFrame() const final { return GetResource()->IsPerFrame(); }

	inline const Ptr<BufferBase> &GetBuffer(uint32_t frame = 0) const final { return GetResource()->GetBuffer(); }
};

// Image
enum class RenderGraphAttachmentLoadOp { kClear, kLoad, kDontCare };

using RenderGraphImageBase = RenderGraphResource<RenderGraphResourceType::kImage>;
template <> class RenderGraphResource<RenderGraphResourceType::kImage> : virtual public RenderGraphResourceBase {
public:
	~RenderGraphResource() override = default;
	inline RenderGraphResourceType GetType() const final { return RenderGraphResourceType::kImage; }
	inline bool IsAlias() const override { return false; }
	virtual const Ptr<ImageView> &GetImageView(uint32_t frame = 0) const = 0;

	virtual RenderGraphAttachmentLoadOp GetLoadOp() const = 0;
	virtual const VkClearValue &GetClearValue() const = 0;
};

class RenderGraphAttachmentBase {
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
};

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

	friend class RenderGraph;
};

using RenderGraphExternalImageBase =
    RenderGraphResource<RenderGraphResourceType::kImage, RenderGraphResourceState::kExternal>;
template <>
class RenderGraphResource<RenderGraphResourceType::kImage, RenderGraphResourceState::kExternal>
    : virtual public RenderGraphImageBase {
public:
	~RenderGraphResource() override = default;

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
	inline explicit RenderGraphSwapchainImage(Ptr<FrameManager> frame_manager)
	    : m_frame_manager{std::move(frame_manager)} {}
	~RenderGraphSwapchainImage() final = default;

	inline static Ptr<RenderGraphSwapchainImage> Create(const Ptr<FrameManager> &frame_manager) {
		return std::make_shared<RenderGraphSwapchainImage>(frame_manager);
	}

	// inline bool IsPerFrame() const final { return true; }

	inline const Ptr<ImageView> &GetImageView(uint32_t frame = 0) const final {
		return m_frame_manager->GetCurrentSwapchainImageView();
	}
};
#endif

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
	template <bool IsAlias /*, typename = std::enable_if_t<State != RenderGraphResourceState::kStatic>*/>
	inline explicit RenderGraphResource(
	    std::weak_ptr<const RenderGraphPassBase> producer,
	    const Ptr<const RenderGraphResource<RenderGraphResourceType::kImage, State, IsAlias>> &resource)
	    : RenderGraphResourceBase(std::move(producer)), m_resource{IsAlias ? resource->GetResource() : resource} {}
	~RenderGraphResource() final = default;

	/* template <bool IsAlias, typename = std::enable_if_t<State != RenderGraphResourceState::kStatic>>
	inline static Ptr<RenderGraphResource>
	Create(const Ptr<RenderGraphPassBase> &producer,
	       const Ptr<RenderGraphResource<RenderGraphResourceType::kImage, State, IsAlias>> &resource) {
	    return std::make_shared<RenderGraphResource>(producer, resource);
	} */

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
	template <bool IsAlias>
	inline RenderGraphResource(
	    std::weak_ptr<const RenderGraphPassBase> producer,
	    const Ptr<const RenderGraphResource<RenderGraphResourceType::kImage, RenderGraphResourceState::kManaged,
	                                        IsAlias>> &resource)
	    : RenderGraphResourceBase(std::move(producer)), m_resources{IsAlias ? resource->GetResources() : resource},
	      m_image_view_type{resource->GetImageViewType()}, m_image_aspects{resource->GetImageAspects()} {}

	template <bool IsAlias>
	inline RenderGraphResource(
	    std::weak_ptr<const RenderGraphPassBase> producer,
	    const Ptr<const RenderGraphResource<RenderGraphResourceType::kImage, RenderGraphResourceState::kManaged,
	                                        IsAlias>> &resource,
	    VkImageViewType image_view_type, VkImageAspectFlags image_aspects)
	    : RenderGraphResourceBase(std::move(producer)), m_resources{IsAlias ? resource->GetResources() : resource},
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

	friend class RenderGraph;
};

class RenderGraphInput {
private:
	Ptr<const RenderGraphResourceBase> m_resource;
	Ptr<Sampler> m_image_sampler;
	RenderGraphInputUsage m_usage;

public:
	inline RenderGraphInput(Ptr<const RenderGraphResourceBase> resource, RenderGraphInputUsage usage,
	                        Ptr<Sampler> image_sampler)
	    : m_resource{std::move(resource)}, m_usage{usage}, m_image_sampler{std::move(image_sampler)} {}

	inline const Ptr<const RenderGraphResourceBase> &GetResource() const { return m_resource; }
	inline RenderGraphInputUsage GetUsage() const { return m_usage; }
	inline const Ptr<Sampler> &GetImageSampler() const { return m_image_sampler; }
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

	template <RenderGraphInputUsage Usage, RenderGraphResourceType Type, RenderGraphResourceState State, bool IsAlias,
	          typename = std::enable_if_t<RenderGraphInputUsageForType<Type>(Usage)>>
	inline RenderGraphInputGroup &PushBack(const std::string &name,
	                                       const Ptr<const RenderGraphResource<Type, State, IsAlias>> &resource) {
		assert(m_input_map.find(name) == m_input_map.end());
		m_input_map[name] = m_inputs.size();
		m_inputs.push_back({resource, Usage});
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
};

enum class RenderGraphPassType { kGraphics, kCompute, kGroup, kCustom, kFinal };
class RenderGraphPassBase : public Base {
public:
	using OwnerWeakPtr = std::variant<std::weak_ptr<RenderGraph>, std::weak_ptr<const RenderGraphPassBase>>;

private:
	std::weak_ptr<RenderGraph> m_render_graph_weak_ptr;
	OwnerWeakPtr m_owner_weak_ptr;

protected:
	template <RenderGraphResourceType Type, RenderGraphResourceState State, bool IsAlias>
	inline auto MakeOutput(const Ptr<Ptr<const RenderGraphResource<Type, State, IsAlias>>> &resource) const {
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
	}

public:
	inline explicit RenderGraphPassBase(const OwnerWeakPtr &owner)
	    : m_owner_weak_ptr{owner}, m_render_graph_weak_ptr{owner.index()
	                                                           ? std::get<1>(owner).lock()->m_render_graph_weak_ptr
	                                                           : std::get<0>(owner)} {}
	/* inline explicit RenderGraphPassBase(const std::weak_ptr<const RenderGraph> &owner)
	    : m_owner_weak_ptr{owner}, m_render_graph_weak_ptr{owner} {}
	inline explicit RenderGraphPassBase(const Ptr<const RenderGraphPassBase> &owner)
	    : m_owner_weak_ptr{owner}, m_render_graph_weak_ptr{owner->m_render_graph_weak_ptr} {}
	inline explicit RenderGraphPassBase(const std::weak_ptr<const RenderGraphPassBase> &owner)
	    : m_owner_weak_ptr{owner}, m_render_graph_weak_ptr{owner.lock()->m_render_graph_weak_ptr} {} */
	virtual ~RenderGraphPassBase() = default;
	// Disable Copy and Move
	inline RenderGraphPassBase(const RenderGraphPassBase &r) = delete;
	inline RenderGraphPassBase &operator=(const RenderGraphPassBase &r) = delete;
	inline RenderGraphPassBase(RenderGraphPassBase &&r) = delete;
	inline RenderGraphPassBase &operator=(RenderGraphPassBase &&r) = delete;

	virtual RenderGraphPassType GetType() const = 0;
	virtual bool UseDescriptors() const { return false; }
	virtual void CmdRun(const Ptr<CommandBuffer> &command_buffer, uint32_t frame) = 0;
	virtual std::vector<RenderGraphInput> GetInputs() const = 0;

	inline bool IsSecondary() const { return m_owner_weak_ptr.index(); }
	inline Ptr<const RenderGraphPassBase> LockParentPass() const {
		return IsSecondary() ? std::get<1>(m_owner_weak_ptr).lock() : nullptr;
	}
	inline Ptr<RenderGraph> LockRenderGraph() const { return m_render_graph_weak_ptr.lock(); }
};

class RenderGraphInfo {
private:
	std::weak_ptr<const RenderGraph> m_owner;

	std::map<std::string, Ptr<RenderGraphPassBase>> m_passes;
	std::map<std::string, RenderGraphInput> m_outputs;

public:
	inline explicit RenderGraphInfo(std::weak_ptr<const RenderGraph> owner) : m_owner{std::move(owner)} {}

	template <typename PassT, typename... Args,
	          typename = std::enable_if_t<std::is_base_of_v<RenderGraphPassBase, PassT>>>
	inline PassT &AddPass(const std::string &name, Args &&...args) {
		assert(m_passes.find(name) == m_passes.end());
		auto ptr = std::make_shared<PassT>(m_owner, std::forward<Args>(args)...);
		PassT &ret = *ptr;
		m_passes.insert({name, std::move(ptr)});
		return ret;
	}

	template <RenderGraphInputUsage Usage, RenderGraphResourceType Type, RenderGraphResourceState State, bool IsAlias,
	          typename = std::enable_if_t<RenderGraphInputUsageForType<Type>(Usage)>>
	inline void SetOutput(const std::string &name,
	                      const Ptr<const RenderGraphResource<Type, State, IsAlias>> &resource) {
		assert(m_outputs.find(name) == m_outputs.end());
		m_outputs[name] = {resource, Usage};
	}

	friend class RenderGraph;
};

class RenderGraph final : public DeviceObjectBase {
private:
	Ptr<Device> m_device_ptr;

	std::map<std::string, Ptr<RenderGraphPassBase>> m_passes;
	std::map<std::string, RenderGraphInput> m_outputs;

	inline void update(const std::function<void(RenderGraphInfo &)> &builder) {
		RenderGraphInfo info{GetSelfPtr<RenderGraph>()};
		builder(info);
		m_passes = std::move(info.m_passes);
		m_outputs = std::move(info.m_outputs);
		SetRecompile();
	}

	bool m_recompile_flag{true};
	void Compile();

public:
	inline explicit RenderGraph(Ptr<Device> device_ptr) : m_device_ptr{std::move(device_ptr)} {}
	inline ~RenderGraph() final = default;

	inline static Ptr<RenderGraph> Create(const Ptr<Device> &device_ptr,
	                                      const std::function<void(RenderGraphInfo &)> &builder) {
		auto ret = std::make_shared<RenderGraph>(device_ptr);
		ret->update(builder);
		return ret;
	}

	template <typename PassT = RenderGraphPassBase,
	          typename = std::enable_if_t<std::is_base_of_v<RenderGraphPassBase, PassT>>>
	PassT &GetPass(const std::string &name) {
		return *std::dynamic_pointer_cast<PassT>(m_passes[name]);
	}

	inline void SetRecompile() { m_recompile_flag = true; }

	void CmdRun(const Ptr<CommandBuffer> &command_buffer, uint32_t frame);

	const Ptr<Device> &GetDevicePtr() const final { return m_device_ptr; }
};

class RenderGraphDescriptorPassBase : public RenderGraphPassBase {
private:
	RenderGraphInputGroup<RenderGraphInputUsageDescriptorEnabled> m_descriptor_group;

	Ptr<DescriptorSetLayout> m_descriptor_layout;
	std::vector<Ptr<DescriptorSet>> m_descriptor_sets;

	void create_descriptors(); // TODO: Implement this

public:
	inline explicit RenderGraphDescriptorPassBase(
	    const OwnerWeakPtr &owner, RenderGraphInputGroup<RenderGraphInputUsageDescriptorEnabled> &&descriptor_group)
	    : RenderGraphPassBase(owner), m_descriptor_group{std::move(descriptor_group)} {
		create_descriptors();
	}

	inline const auto &GetDescriptorGroup() const { return m_descriptor_group; }
	// auto &GetDescriptorGroup() { return m_descriptor_group; }
	// virtual void OnDescriptorCreated() {} // TODO: Better design

	inline const auto &GetDescriptorLayout() const { return m_descriptor_layout; }
	inline const auto &GetDescriptorSet(uint32_t frame) const {
		return m_descriptor_sets.size() == 1 ? m_descriptor_sets.front() : m_descriptor_sets[frame];
	}

	bool UseDescriptors() const final { return true; }
};

class RenderGraphGraphicsPassBase : public RenderGraphDescriptorPassBase {
public:
	inline RenderGraphPassType GetType() const final { return RenderGraphPassType::kGraphics; }
};
class RenderGraphComputePassBase : public RenderGraphDescriptorPassBase {
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

class RenderGraphPassGroupBase : public RenderGraphPassBase {
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

#undef USAGE_TO_BASE
#undef USAGE_TO_READ_ONLY

} // namespace myvk

#endif
