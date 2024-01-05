#pragma once
#ifndef MYVK_RG_RESOURCEPOOL_HPP
#define MYVK_RG_RESOURCEPOOL_HPP

#include "Pool.hpp"
#include "Resource.hpp"

namespace myvk_rg::interface {

template <typename Derived>
class ResourcePool : public Pool<Derived, std::variant<ManagedBuffer, ExternalBufferBase, LastFrameBuffer, ManagedImage,
                                                       CombinedImage, ExternalImageBase, LastFrameImage>> {
private:
	using PoolBase = Pool<Derived, std::variant<ManagedBuffer, ExternalBufferBase, LastFrameBuffer, ManagedImage,
	                                            CombinedImage, ExternalImageBase, LastFrameImage>>;

public:
	inline ResourcePool() = default;
	inline ResourcePool(ResourcePool &&) noexcept = default;
	inline ~ResourcePool() override = default;

	inline const auto &GetResourcePoolData() const { return PoolBase::GetPoolData(); }

protected:
	template <typename Type, typename... Args,
	          typename = std::enable_if_t<std::is_base_of_v<BufferBase, Type> || std::is_base_of_v<ImageBase, Type>>>
	inline Type *CreateResource(const PoolKey &resource_key, Args &&...args) {
		return PoolBase::template Construct<0, Type>(resource_key, std::forward<Args>(args)...);
	}
	inline void DeleteResource(const PoolKey &resource_key) { return PoolBase::Delete(resource_key); }

	template <typename BufferType = BufferBase, typename = std::enable_if_t<std::is_base_of_v<BufferBase, BufferType> ||
	                                                                        std::is_same_v<BufferBase, BufferType>>>
	inline BufferType *GetBufferResource(const PoolKey &resource_buffer_key) const {
		return PoolBase::template Get<0, BufferType>(resource_buffer_key);
	}
	template <typename ImageType = ImageBase, typename = std::enable_if_t<std::is_base_of_v<ImageBase, ImageType> ||
	                                                                      std::is_same_v<ImageBase, ImageType>>>
	inline ImageType *GetImageResource(const PoolKey &resource_image_key) const {
		return PoolBase::template Get<0, ImageType>(resource_image_key);
	}
	template <typename ResourceType = ResourceBase,
	          typename = std::enable_if_t<std::is_base_of_v<ResourceBase, ResourceType> ||
	                                      std::is_same_v<ResourceBase, ResourceType>>>
	inline ResourceType *GetResource(const PoolKey &resource_image_key) const {
		return PoolBase::template Get<0, ResourceType>(resource_image_key);
	}
	inline void ClearResources() { PoolBase::Clear(); }
};

} // namespace myvk_rg::interface

#endif // MYVK_RESOURCEPOOL_HPP
