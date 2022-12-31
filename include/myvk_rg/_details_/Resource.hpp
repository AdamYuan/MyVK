#ifndef MYVK_RG_RESOURCE_HPP
#define MYVK_RG_RESOURCE_HPP

#include <myvk/Buffer.hpp>
#include <myvk/FrameManager.hpp>
#include <myvk/ImageView.hpp>

#include "Macro.hpp"
#include "Pool.hpp"
#include "RenderGraphBase.hpp"
#include "ResourceBase.hpp"

#include <cassert>
#include <cinttypes>
#include <type_traits>

namespace myvk_rg::_details_ {
class BufferBase : public ResourceBase {
public:
	// inline constexpr ResourceType GetType() const { return ResourceType::kBuffer; }

	inline ~BufferBase() override = default;
	inline explicit BufferBase(ResourceState state) : ResourceBase(MakeResourceClass(ResourceType::kBuffer, state)) {}
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
	inline explicit ImageBase(ResourceState state) : ResourceBase(MakeResourceClass(ResourceType::kImage, state)) {}
	inline ImageBase(ImageBase &&) noexcept = default;

	template <typename Visitor> std::invoke_result_t<Visitor, ResourceBase *> inline Visit(Visitor &&visitor);
	template <typename Visitor> std::invoke_result_t<Visitor, ResourceBase *> inline Visit(Visitor &&visitor) const;

	inline const myvk::Ptr<myvk::ImageView> &GetVkImageView() const {
		return Visit([](auto *image) -> const myvk::Ptr<myvk::ImageView> & { return image->GetVkImageView(); });
	};
};

// TODO: Attachment
template <typename Derived> class ImageAttachmentInfo {
private:
	inline RenderGraphBase *get_render_graph_ptr() {
		static_assert(std::is_base_of_v<ObjectBase, Derived>);
		return static_cast<ObjectBase *>(static_cast<Derived *>(this))->GetRenderGraphPtr();
	}
	VkAttachmentLoadOp m_load_op{VK_ATTACHMENT_LOAD_OP_DONT_CARE};
	VkClearValue m_clear_value{};

public:
	inline ImageAttachmentInfo() = default;
	inline ImageAttachmentInfo(VkAttachmentLoadOp load_op, const VkClearValue &clear_value)
	    : m_load_op{load_op}, m_clear_value{clear_value} {
		static_assert(std::is_base_of_v<ObjectBase, Derived>);
	}

	inline void SetLoadOp(VkAttachmentLoadOp load_op) {
		if (m_load_op != load_op) {
			m_load_op = load_op;
			get_render_graph_ptr()->m_compile_phrase.generate_vk_render_pass = true;
		}
	}
	inline void SetClearColorValue(const VkClearColorValue &clear_color_value) {
		m_clear_value.color = clear_color_value;
	}
	inline void SetClearDepthStencilValue(const VkClearDepthStencilValue &clear_depth_stencil_value) {
		m_clear_value.depthStencil = clear_depth_stencil_value;
	}

	inline VkAttachmentLoadOp GetLoadOp() const { return m_load_op; }
	inline const VkClearValue &GetClearValue() const { return m_clear_value; }
};

// External
// TODO: External barriers (pipeline stage + access + image layout, begin & end)
class ExternalImageBase : public ImageBase, public ImageAttachmentInfo<ExternalImageBase> {
public:
	inline ExternalImageBase() : ImageBase(ResourceState::kExternal) {}
	inline ExternalImageBase(ExternalImageBase &&) noexcept = default;
	inline ~ExternalImageBase() override = default;

public:
	// inline constexpr ResourceState GetState() const { return ResourceState::kExternal; }

	virtual const myvk::Ptr<myvk::ImageView> &GetVkImageView() const = 0;
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

	MYVK_RG_OBJECT_FRIENDS
	MYVK_RG_INLINE_INITIALIZER(const myvk::Ptr<myvk::FrameManager> &frame_manager) { m_frame_manager = frame_manager; }

public:
	inline SwapchainImage() = default;
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
	const ImageBase *m_pointed_image{};

	MYVK_RG_OBJECT_FRIENDS
	MYVK_RG_INLINE_INITIALIZER(ImageBase *image) {
		m_pointed_image = image->Visit([](auto *image) -> const ImageBase * {
			if constexpr (ResourceVisitorTrait<decltype(image)>::kState == ResourceState::kAlias)
				return image->GetPointedResource();
			return image;
		});
	}

public:
	// inline constexpr ResourceState GetState() const { return ResourceState::kAlias; }

	inline ImageAlias() : ImageBase(ResourceState::kAlias) {}
	inline ImageAlias(ImageAlias &&) noexcept = default;
	inline ~ImageAlias() final = default;

	inline const ImageBase *GetPointedResource() const { return m_pointed_image; }

	inline const myvk::Ptr<myvk::ImageView> &GetVkImageView() const { return m_pointed_image->GetVkImageView(); }
};
class BufferAlias final : public BufferBase {
private:
	const BufferBase *m_pointed_buffer{};

	MYVK_RG_OBJECT_FRIENDS
	MYVK_RG_INLINE_INITIALIZER(BufferBase *buffer) {
		m_pointed_buffer = buffer->Visit([](auto *buffer) -> const BufferBase * {
			if constexpr (ResourceVisitorTrait<decltype(buffer)>::kState == ResourceState::kAlias)
				return buffer->GetPointedResource();
			return buffer;
		});
	}

public:
	// inline constexpr ResourceState GetState() const { return ResourceState::kAlias; }

	inline BufferAlias() : BufferBase(ResourceState::kAlias) {}
	inline BufferAlias(BufferAlias &&) = default;
	inline ~BufferAlias() final = default;

	inline const BufferBase *GetPointedResource() const { return m_pointed_buffer; }

	inline const myvk::Ptr<myvk::BufferBase> &GetVkBuffer() const { return m_pointed_buffer->GetVkBuffer(); }
};

// Managed Resources
template <typename Derived, typename SizeType> class ManagedResourceInfo {
public:
	using SizeFunc = std::function<SizeType(const VkExtent2D &)>;

private:
	inline RenderGraphBase *get_render_graph_ptr() {
		static_assert(std::is_base_of_v<ObjectBase, Derived>);
		return static_cast<ObjectBase *>(static_cast<Derived *>(this))->GetRenderGraphPtr();
	}

	bool m_persistence{false};
	SizeType m_size{};
	SizeFunc m_size_func{};

public:
	inline bool GetPersistence() const { return m_persistence; }
	inline void SetPersistence(bool persistence = true) {
		if (m_persistence != persistence) {
			m_persistence = persistence;
			get_render_graph_ptr()->m_compile_phrase.generate_vk_resource = true; // TODO: or merge_subpass
		}
	}
	inline const SizeType &GetSize() const { return m_size; }
	inline void SetSize(const SizeType &size) {
		if (m_size != size) {
			m_size = size;
			get_render_graph_ptr()->m_compile_phrase.generate_vk_resource = true;
		}
	}
	inline const SizeFunc &GetSizeFunc() const { return m_size_func; }
	template <typename Func> inline void SetSizeFunc(Func &&func) {
		m_size_func = func;
		SetSize(m_size_func(get_render_graph_ptr()->m_canvas_size));
	}
};

class ManagedBuffer final : public BufferBase, public ManagedResourceInfo<ManagedBuffer, VkDeviceSize> {
private:
	bool m_persistent{false};

	MYVK_RG_OBJECT_FRIENDS
	MYVK_RG_INLINE_INITIALIZER() {}

public:
	// inline constexpr ResourceState GetState() const { return ResourceState::kManaged; }

	inline ManagedBuffer() : BufferBase(ResourceState::kManaged) {}
	inline ManagedBuffer(ManagedBuffer &&) noexcept = default;
	~ManagedBuffer() override = default;

	inline const myvk::Ptr<myvk::BufferBase> &GetVkBuffer() const {
		static myvk::Ptr<myvk::BufferBase> x;
		return x;
	}
};

class ImageSize {
private:
	VkExtent3D m_extent;
	uint32_t m_base_mip_level, m_mip_levels;

public:
	inline ImageSize() = default;
	inline explicit ImageSize(const VkExtent2D &extent_2d, uint32_t layer_count = 1, uint32_t base_mip_level = 0,
	                          uint32_t mip_levels = 1)
	    : m_extent{extent_2d.width, extent_2d.height, layer_count}, m_base_mip_level{base_mip_level}, m_mip_levels{
	                                                                                                      mip_levels} {}
	inline explicit ImageSize(const VkExtent3D &extent_3d, uint32_t base_mip_level = 0, uint32_t mip_levels = 1)
	    : m_extent{extent_3d}, m_base_mip_level{base_mip_level}, m_mip_levels{mip_levels} {}
	inline ImageSize(ImageSize &&) noexcept = default;

	inline const VkExtent3D &GetExtent() const { return m_extent; }
	inline uint32_t GetBaseMipLevel() const { return m_base_mip_level; }
	inline uint32_t GetMipLevelCount() const { return m_mip_levels; }
	inline uint32_t GetArrayLayers() const { return m_extent.depth; }

	// TODO: Implement this
	inline void Merge1D(const ImageSize &r) {}
	inline void Merge2D(const ImageSize &r) {}
	inline bool Merge3D(const ImageSize &r) {}
};
class ManagedImage final : public ImageBase,
                           public ImageAttachmentInfo<ManagedImage>,
                           public ManagedResourceInfo<ManagedImage, ImageSize> {
private:
	bool m_persistent{false};

	MYVK_RG_OBJECT_FRIENDS
	MYVK_RG_INLINE_INITIALIZER() {}

public:
	// inline constexpr ResourceState GetState() const { return ResourceState::kManaged; }

	inline ManagedImage() : ImageBase(ResourceState::kManaged) {}
	inline ManagedImage(ManagedImage &&) noexcept = default;
	~ManagedImage() override = default;

	inline const myvk::Ptr<myvk::ImageView> &GetVkImageView() const {
		static myvk::Ptr<myvk::ImageView> x;
		return x;
	}
};

class CombinedImage final : public ImageBase {
private:
	VkImageViewType m_view_type;
	std::vector<const ImageBase *> m_images;

	MYVK_RG_OBJECT_FRIENDS
	MYVK_RG_INLINE_INITIALIZER(VkImageViewType view_type, std::vector<const ImageBase *> &&images) {
		m_view_type = view_type;
		m_images = std::move(images);
	}
	template <typename Iterator>
	MYVK_RG_INLINE_INITIALIZER(VkImageViewType view_type, Iterator images_begin, Iterator images_end) {
		m_view_type = view_type;
		m_images = {images_begin, images_end};
	}

public:
	// inline constexpr ResourceState GetState() const { return ResourceState::kCombinedImage; }

	inline CombinedImage() : ImageBase(ResourceState::kManaged) {}
	inline CombinedImage(CombinedImage &&) noexcept = default;
	inline const std::vector<const ImageBase *> &GetImages() const { return m_images; }
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
	return visitor(static_cast<BufferAlias *>(nullptr));
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
	return visitor(static_cast<const BufferAlias *>(nullptr));
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
	return visitor(static_cast<ImageAlias *>(nullptr));
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
	return visitor(static_cast<const ImageAlias *>(nullptr));
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
	return visitor(static_cast<BufferAlias *>(nullptr));
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
	return visitor(static_cast<const BufferAlias *>(nullptr));
}

template <typename Derived>
class ResourcePool
    : public Pool<Derived, PoolVariant<ManagedBuffer, ExternalBufferBase, ManagedImage, ExternalImageBase>> {
private:
	using _ResourcePool =
	    Pool<Derived, PoolVariant<ManagedBuffer, ExternalBufferBase, ManagedImage, ExternalImageBase>>;

public:
	inline ResourcePool() = default;
	inline ResourcePool(ResourcePool &&) noexcept = default;
	inline ~ResourcePool() override = default;

protected:
	template <typename Type, typename... Args,
	          typename = std::enable_if_t<std::is_base_of_v<BufferBase, Type> || std::is_base_of_v<ImageBase, Type>>>
	inline Type *CreateResource(const PoolKey &resource_key, Args &&...args) {
		return _ResourcePool::template CreateAndInitialize<0, Type, Args...>(resource_key, std::forward<Args>(args)...);
	}
	inline void DeleteResource(const PoolKey &resource_key) { return _ResourcePool::Delete(resource_key); }

	template <typename BufferType = BufferBase, typename = std::enable_if_t<std::is_base_of_v<BufferBase, BufferType> ||
	                                                                        std::is_same_v<BufferBase, BufferType>>>
	inline BufferType *GetBufferResource(const PoolKey &resource_buffer_key) const {
		return _ResourcePool::template Get<0, BufferType>(resource_buffer_key);
	}
	template <typename ImageType = ImageBase, typename = std::enable_if_t<std::is_base_of_v<ImageBase, ImageType> ||
	                                                                      std::is_same_v<ImageBase, ImageType>>>
	inline ImageType *GetImageResource(const PoolKey &resource_image_key) const {
		return _ResourcePool::template Get<0, ImageType>(resource_image_key);
	}
	template <typename ResourceType = ResourceBase,
	          typename = std::enable_if_t<std::is_base_of_v<ResourceBase, ResourceType> ||
	                                      std::is_same_v<ResourceBase, ResourceType>>>
	inline ResourceType *GetResource(const PoolKey &resource_image_key) const {
		return _ResourcePool::template Get<0, ResourceType>(resource_image_key);
	}
	inline void ClearResources() { _ResourcePool::Clear(); }
};

} // namespace myvk_rg::_details_

#endif
