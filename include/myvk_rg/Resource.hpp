#ifndef MYVK_RG_RESOURCE_HPP
#define MYVK_RG_RESOURCE_HPP

#include "../myvk/FrameManager.hpp"
#include "ObjectBase.hpp"
#include <cassert>
#include <cinttypes>
#include <type_traits>

namespace myvk_rg {
// Resource Base and Types
enum class ResourceType : uint8_t { kImage, kBuffer };
enum class ResourceState : uint8_t { kManaged, kCombinedImage, kExternal, kAlias };
#define MAKE_RESOURCE_CLASS_VAL(Type, State) uint8_t(static_cast<uint8_t>(State) << 1u | static_cast<uint8_t>(Type))
enum class ResourceClass : uint8_t {
	kManagedImage = MAKE_RESOURCE_CLASS_VAL(ResourceType::kImage, ResourceState::kManaged),
	kExternalImageBase = MAKE_RESOURCE_CLASS_VAL(ResourceType::kImage, ResourceState::kExternal),
	kCombinedImage = MAKE_RESOURCE_CLASS_VAL(ResourceType::kImage, ResourceState::kCombinedImage),
	kImageAlias = MAKE_RESOURCE_CLASS_VAL(ResourceType::kImage, ResourceState::kAlias),
	kManagedBuffer = MAKE_RESOURCE_CLASS_VAL(ResourceType::kBuffer, ResourceState::kManaged),
	kExternalBufferBase = MAKE_RESOURCE_CLASS_VAL(ResourceType::kBuffer, ResourceState::kExternal),
	kBufferAlias = MAKE_RESOURCE_CLASS_VAL(ResourceType::kBuffer, ResourceState::kAlias)
};
#define MAKE_RESOURCE_CLASS(Type, State) static_cast<ResourceClass>(MAKE_RESOURCE_CLASS_VAL(Type, State))

class ManagedImage;
class ExternalImageBase;
class CombinedImage;
class ImageAlias;
class ManagedBuffer;
class ExternalBufferBase;
class BufferAlias;

namespace _details_resource_trait_ {
template <typename T> struct ResourceTrait;
template <> struct ResourceTrait<ManagedImage> {
	static constexpr ResourceType kType = ResourceType::kImage;
	static constexpr ResourceState kState = ResourceState::kManaged;
};
template <> struct ResourceTrait<ExternalImageBase> {
	static constexpr ResourceType kType = ResourceType::kImage;
	static constexpr ResourceState kState = ResourceState::kExternal;
};
template <> struct ResourceTrait<CombinedImage> {
	static constexpr ResourceType kType = ResourceType::kImage;
	static constexpr ResourceState kState = ResourceState::kCombinedImage;
};
template <> struct ResourceTrait<ImageAlias> {
	static constexpr ResourceType kType = ResourceType::kImage;
	static constexpr ResourceState kState = ResourceState::kAlias;
};
template <> struct ResourceTrait<ManagedBuffer> {
	static constexpr ResourceType kType = ResourceType::kBuffer;
	static constexpr ResourceState kState = ResourceState::kManaged;
};
template <> struct ResourceTrait<ExternalBufferBase> {
	static constexpr ResourceType kType = ResourceType::kBuffer;
	static constexpr ResourceState kState = ResourceState::kExternal;
};
template <> struct ResourceTrait<BufferAlias> {
	static constexpr ResourceType kType = ResourceType::kBuffer;
	static constexpr ResourceState kState = ResourceState::kAlias;
};
} // namespace _details_resource_trait_

// Used in Visit function
template <typename RawType> class ResourceVisitorTrait {
private:
	using Type = std::remove_pointer_t<std::decay_t<RawType>>;

public:
	static constexpr ResourceType kType = _details_resource_trait_::ResourceTrait<Type>::kType;
	static constexpr ResourceState kState = _details_resource_trait_::ResourceTrait<Type>::kState;
	static constexpr ResourceClass kClass = MAKE_RESOURCE_CLASS(kType, kState);
};

class ResourceBase : public ObjectBase {
private:
	ResourceClass m_class{};
	PassBase *m_producer_pass_ptr{};

	inline void set_producer_pass_ptr(PassBase *producer_pass_ptr) { m_producer_pass_ptr = producer_pass_ptr; }

	template <typename, typename...> friend class Pool;
	template <typename> friend class AliasOutputPool;

public:
	inline ~ResourceBase() override = default;
	inline ResourceBase(ResourceClass resource_class) : m_class{resource_class} {}
	inline ResourceBase(ResourceBase &&) noexcept = default;

	inline ResourceType GetType() const { return static_cast<ResourceType>(static_cast<uint8_t>(m_class) & 1u); }
	inline ResourceState GetState() const { return static_cast<ResourceState>(static_cast<uint8_t>(m_class) >> 1u); }

	// TODO: Is that actually needed ?
	template <typename Visitor> std::invoke_result_t<Visitor, ResourceBase *> Visit(Visitor &&visitor);
	template <typename Visitor> std::invoke_result_t<Visitor, ResourceBase *> Visit(Visitor &&visitor) const;
	// virtual bool IsPerFrame() const = 0;
	// virtual void Resize(uint32_t width, uint32_t height) {}

	inline PassBase *GetProducerPassPtr() const { return m_producer_pass_ptr; }
};

class BufferBase : public ResourceBase {
public:
	// inline constexpr ResourceType GetType() const { return ResourceType::kBuffer; }

	inline ~BufferBase() override = default;
	inline explicit BufferBase(ResourceState state) : ResourceBase(MAKE_RESOURCE_CLASS(ResourceType::kBuffer, state)) {}
	inline BufferBase(BufferBase &&) noexcept = default;

	template <typename Visitor> std::invoke_result_t<Visitor, ResourceBase *> inline Visit(Visitor &&visitor);
	template <typename Visitor> std::invoke_result_t<Visitor, ResourceBase *> inline Visit(Visitor &&visitor) const;

	inline const myvk::Ptr<myvk::BufferBase> &GetVkBuffer() const {
		return Visit([](auto *buffer) -> const myvk::Ptr<myvk::BufferBase> & { return buffer->GetVkBuffer(); });
	};
};

class ImageBase : public ResourceBase {
public:
	// inline constexpr ResourceType GetType() const { return ResourceType::kImage; }

	inline ~ImageBase() override = default;
	inline explicit ImageBase(ResourceState state) : ResourceBase(MAKE_RESOURCE_CLASS(ResourceType::kImage, state)) {}
	inline ImageBase(ImageBase &&) noexcept = default;

	template <typename Visitor> std::invoke_result_t<Visitor, ResourceBase *> inline Visit(Visitor &&visitor);
	template <typename Visitor> std::invoke_result_t<Visitor, ResourceBase *> inline Visit(Visitor &&visitor) const;

	inline const myvk::Ptr<myvk::ImageView> &GetVkImageView() const {
		return Visit([](auto *image) -> const myvk::Ptr<myvk::ImageView> & { return image->GetVkImageView(); });
	};

	// virtual AttachmentLoadOp GetLoadOp() const = 0;
	// virtual const VkClearValue &GetClearValue() const = 0;
};

// TODO: Attachment
enum class AttachmentLoadOp { kClear, kLoad, kDontCare };
class ImageAttachmentInfo {};

// External
// TODO: External barriers (pipeline stage + access + image layout, begin & end)
class ExternalImageBase : public ImageBase {
public:
	inline ExternalImageBase() : ImageBase(ResourceState::kExternal) {}
	inline ExternalImageBase(ExternalImageBase &&) noexcept = default;
	inline ~ExternalImageBase() override = default;

private:
	AttachmentLoadOp m_load_op{AttachmentLoadOp::kDontCare};
	VkClearValue m_clear_value{};

public:
	// inline constexpr ResourceState GetState() const { return ResourceState::kExternal; }

	virtual const myvk::Ptr<myvk::ImageView> &GetVkImageView() const = 0;

	template <AttachmentLoadOp LoadOp, typename = std::enable_if_t<LoadOp != AttachmentLoadOp::kClear>>
	inline void SetLoadOp() {
		m_load_op = LoadOp;
	}
	template <AttachmentLoadOp LoadOp, typename = std::enable_if_t<LoadOp == AttachmentLoadOp::kClear>>
	inline void SetLoadOp(const VkClearValue &clear_value) {
		m_load_op = LoadOp;
		m_clear_value = clear_value;
	}
	inline AttachmentLoadOp GetLoadOp() const { return m_load_op; }
	inline const VkClearValue &GetClearValue() const { return m_clear_value; }
};
class ExternalBufferBase : public BufferBase {
public:
	// inline constexpr ResourceState GetState() const { return ResourceState::kExternal; }

	virtual const myvk::Ptr<myvk::BufferBase> &GetVkBuffer() const = 0;

	inline ExternalBufferBase() : BufferBase(ResourceState::kExternal) {}
	inline ExternalBufferBase(ExternalBufferBase &&) noexcept = default;
	inline ~ExternalBufferBase() override = default;
};
#ifdef MYVK_ENABLE_GLFW
class SwapchainImage final : public ExternalImageBase {
private:
	myvk::Ptr<myvk::FrameManager> m_frame_manager;

public:
	inline explicit SwapchainImage(myvk::Ptr<myvk::FrameManager> frame_manager)
	    : m_frame_manager{std::move(frame_manager)} {}
	inline SwapchainImage(SwapchainImage &&) noexcept = default;
	~SwapchainImage() final = default;
	inline const myvk::Ptr<myvk::ImageView> &GetVkImageView() const final {
		return m_frame_manager->GetCurrentSwapchainImageView();
	}
};
#endif
// Alias
class ImageAlias final : public ImageBase {
private:
	const ImageBase *m_pointed_image;

public:
	// inline constexpr ResourceState GetState() const { return ResourceState::kAlias; }

	inline explicit ImageAlias(ImageBase *image)
	    : ImageBase(ResourceState::kAlias), m_pointed_image{image->Visit([](auto *image) -> const ImageBase * {
		      if constexpr (ResourceVisitorTrait<decltype(image)>::kState == ResourceState::kAlias)
			      return image->GetPointedResource();
		      return image;
	      })} {}
	inline ImageAlias(ImageAlias &&) noexcept = default;
	inline ~ImageAlias() final = default;

	inline const ImageBase *GetPointedResource() const { return m_pointed_image; }

	inline const myvk::Ptr<myvk::ImageView> &GetVkImageView() const { return m_pointed_image->GetVkImageView(); }
};
class BufferAlias final : public BufferBase {
private:
	const BufferBase *m_pointed_buffer;

public:
	// inline constexpr ResourceState GetState() const { return ResourceState::kAlias; }

	inline explicit BufferAlias(BufferBase *buffer)
	    : BufferBase(ResourceState::kAlias), m_pointed_buffer{buffer->Visit([](auto *buffer) -> const BufferBase * {
		      if constexpr (ResourceVisitorTrait<decltype(buffer)>::kState == ResourceState::kAlias)
			      return buffer->GetPointedResource();
		      return buffer;
	      })} {}
	inline BufferAlias(BufferAlias &&) = default;
	inline ~BufferAlias() final = default;

	inline const BufferBase *GetPointedResource() const { return m_pointed_buffer; }

	inline const myvk::Ptr<myvk::BufferBase> &GetVkBuffer() const { return m_pointed_buffer->GetVkBuffer(); }
};

// Managed Resources
// TODO: Complete this
class ManagedBuffer final : public BufferBase {
private:
	bool m_persistent{false};

public:
	// inline constexpr ResourceState GetState() const { return ResourceState::kManaged; }

	inline ManagedBuffer() : BufferBase(ResourceState::kManaged) {}
	inline ManagedBuffer(ManagedBuffer &&) noexcept = default;
	~ManagedBuffer() override = default;

	const myvk::Ptr<myvk::BufferBase> &GetVkBuffer() const {
		static myvk::Ptr<myvk::BufferBase> x;
		return x;
	}
};
class ManagedImage final : public ImageBase {
public:
	// inline constexpr ResourceState GetState() const { return ResourceState::kManaged; }

	inline ManagedImage() : ImageBase(ResourceState::kManaged) {}
	inline ManagedImage(ManagedImage &&) noexcept = default;
	~ManagedImage() override = default;

	inline const myvk::Ptr<myvk::ImageView> &GetVkImageView() const {
		static myvk::Ptr<myvk::ImageView> x;
		return x;
	}

	inline AttachmentLoadOp GetLoadOp() const { return {}; }
	const VkClearValue &GetClearValue() const {
		static VkClearValue x;
		return x;
	}
};

class CombinedImage final : public ImageBase {
public:
	// inline constexpr ResourceState GetState() const { return ResourceState::kCombinedImage; }

	inline CombinedImage() : ImageBase(ResourceState::kManaged) {}
	inline CombinedImage(CombinedImage &&) noexcept = default;
	~CombinedImage() override = default;
};

template <typename Visitor> std::invoke_result_t<Visitor, ResourceBase *> ResourceBase::Visit(Visitor &&visitor) {
	switch (m_class) {
	case ResourceClass::kManagedImage:
		return visitor(static_cast<ManagedImage *>(this));
	case ResourceClass::kExternalImageBase:
		return visitor(static_cast<ExternalImageBase *>(this));
	case ResourceClass::kCombinedImage:
		return visitor(static_cast<CombinedImage *>(this));
	case ResourceClass::kImageAlias:
		return visitor(static_cast<ImageAlias *>(this));
	case ResourceClass::kManagedBuffer:
		return visitor(static_cast<ManagedBuffer *>(this));
	case ResourceClass::kExternalBufferBase:
		return visitor(static_cast<ExternalBufferBase *>(this));
	case ResourceClass::kBufferAlias:
		return visitor(static_cast<BufferAlias *>(this));
	}
	assert(false);
	return {};
}
template <typename Visitor> std::invoke_result_t<Visitor, ResourceBase *> ResourceBase::Visit(Visitor &&visitor) const {
	switch (m_class) {
	case ResourceClass::kManagedImage:
		return visitor(static_cast<const ManagedImage *>(this));
	case ResourceClass::kExternalImageBase:
		return visitor(static_cast<const ExternalImageBase *>(this));
	case ResourceClass::kCombinedImage:
		return visitor(static_cast<const CombinedImage *>(this));
	case ResourceClass::kImageAlias:
		return visitor(static_cast<const ImageAlias *>(this));
	case ResourceClass::kManagedBuffer:
		return visitor(static_cast<const ManagedBuffer *>(this));
	case ResourceClass::kExternalBufferBase:
		return visitor(static_cast<const ExternalBufferBase *>(this));
	case ResourceClass::kBufferAlias:
		return visitor(static_cast<const BufferAlias *>(this));
	}
	assert(false);
	return {};
}

template <typename Visitor> std::invoke_result_t<Visitor, ResourceBase *> ImageBase::Visit(Visitor &&visitor) {
	switch (GetState()) {
	case ResourceState::kManaged:
		return visitor(static_cast<ManagedImage *>(this));
	case ResourceState::kExternal:
		return visitor(static_cast<ExternalImageBase *>(this));
	case ResourceState::kCombinedImage:
		return visitor(static_cast<CombinedImage *>(this));
	case ResourceState::kAlias:
		return visitor(static_cast<ImageAlias *>(this));
	}
	assert(false);
	return {};
}
template <typename Visitor> std::invoke_result_t<Visitor, ResourceBase *> ImageBase::Visit(Visitor &&visitor) const {
	switch (GetState()) {
	case ResourceState::kManaged:
		return visitor(static_cast<const ManagedImage *>(this));
	case ResourceState::kExternal:
		return visitor(static_cast<const ExternalImageBase *>(this));
	case ResourceState::kCombinedImage:
		return visitor(static_cast<const CombinedImage *>(this));
	case ResourceState::kAlias:
		return visitor(static_cast<const ImageAlias *>(this));
	}
	assert(false);
	return {};
}

template <typename Visitor> std::invoke_result_t<Visitor, ResourceBase *> BufferBase::Visit(Visitor &&visitor) {
	switch (GetState()) {
	case ResourceState::kManaged:
		return visitor(static_cast<ManagedBuffer *>(this));
	case ResourceState::kExternal:
		return visitor(static_cast<ExternalBufferBase *>(this));
	case ResourceState::kAlias:
		return visitor(static_cast<BufferAlias *>(this));
	default:
		assert(false);
	}
	return {};
}
template <typename Visitor> std::invoke_result_t<Visitor, ResourceBase *> BufferBase::Visit(Visitor &&visitor) const {
	switch (GetState()) {
	case ResourceState::kManaged:
		return visitor(static_cast<const ManagedBuffer *>(this));
	case ResourceState::kExternal:
		return visitor(static_cast<const ExternalBufferBase *>(this));
	case ResourceState::kAlias:
		return visitor(static_cast<const BufferAlias *>(this));
	default:
		assert(false);
	}
	return {};
}

} // namespace myvk_rg

#undef MAKE_RESOURCE_CLASS_VAL
#undef MAKE_RESOURCE_CLASS

#endif
