#ifndef MYVK_RG_RESOURCE_BASE_HPP
#define MYVK_RG_RESOURCE_BASE_HPP

#include "ObjectBase.hpp"
#include <cinttypes>
#include <type_traits>

namespace myvk_rg::_details_ {

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
inline constexpr ResourceClass MakeResourceClass(ResourceType type, ResourceState state) {
	return static_cast<ResourceClass>(MAKE_RESOURCE_CLASS_VAL(type, state));
}
#undef MAKE_RESOURCE_CLASS_VAL

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
	using Type = std::decay_t<std::remove_pointer_t<std::decay_t<RawType>>>;

public:
	static constexpr ResourceType kType = _details_resource_trait_::ResourceTrait<Type>::kType;
	static constexpr ResourceState kState = _details_resource_trait_::ResourceTrait<Type>::kState;
	static constexpr ResourceClass kClass = MakeResourceClass(kType, kState);
	static constexpr bool kIsCombinedOrManagedImage =
	    kClass == ResourceClass::kCombinedImage || kClass == ResourceClass::kManagedImage;
	static constexpr bool kIsCombinedImageChild = kClass == ResourceClass::kCombinedImage ||
	                                              kClass == ResourceClass::kManagedImage ||
	                                              kClass == ResourceClass::kImageAlias;
};

class ResourceBase : public ObjectBase {
private:
	ResourceClass m_class{};
	PassBase *m_producer_pass_ptr{};

	inline void set_producer_pass_ptr(PassBase *producer_pass_ptr) { m_producer_pass_ptr = producer_pass_ptr; }

	template <typename, typename...> friend class Pool;
	template <typename> friend class AliasOutputPool;
	friend class CombinedImage;

public:
	inline ~ResourceBase() override = default;
	inline ResourceBase(ResourceClass resource_class) : m_class{resource_class} {}
	inline ResourceBase(ResourceBase &&) noexcept = default;

	inline ResourceType GetType() const { return static_cast<ResourceType>(static_cast<uint8_t>(m_class) & 1u); }
	inline ResourceState GetState() const { return static_cast<ResourceState>(static_cast<uint8_t>(m_class) >> 1u); }
	inline ResourceClass GetClass() const { return m_class; }

	// TODO: Is that actually needed ? (currently implemented in Resource.hpp)
	template <typename Visitor> std::invoke_result_t<Visitor, ResourceBase *> Visit(Visitor &&visitor);
	template <typename Visitor> std::invoke_result_t<Visitor, ResourceBase *> Visit(Visitor &&visitor) const;
	// virtual bool IsPerFrame() const = 0;
	// virtual void Resize(uint32_t width, uint32_t height) {}

	inline PassBase *GetProducerPassPtr() const { return m_producer_pass_ptr; }
};

} // namespace myvk_rg::_details_

#endif
