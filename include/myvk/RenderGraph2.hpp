#ifndef MYVK_RENDER_GRAPH_HPP
#define MYVK_RENDER_GRAPH_HPP

#include "CommandBuffer.hpp"
#include "FrameManager.hpp"
#include <cassert>
#include <climits>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>

namespace myvk_rg {
///////////////////////////
// SECTION: Base Objects //
///////////////////////////
#pragma region SECTION : Base Objects

class RenderGraphBase;
class PassBase;
// Object Base
class PoolKey;
class ObjectBase {
private:
	RenderGraphBase *m_render_graph_ptr{};
	const PoolKey *m_key_ptr{};

	inline void set_render_graph_ptr(RenderGraphBase *render_graph_ptr) { m_render_graph_ptr = render_graph_ptr; }
	inline void set_key_ptr(const PoolKey *key_ptr) { m_key_ptr = key_ptr; }

	template <typename, typename...> friend class Pool;
	template <typename> friend class DescriptorInputSlot;

public:
	inline ObjectBase() = default;
	inline virtual ~ObjectBase() = default;

	inline RenderGraphBase *GetRenderGraphPtr() const { return m_render_graph_ptr; }
	inline const PoolKey &GetKey() const { return *m_key_ptr; }

	// Disable Copy
	inline ObjectBase(ObjectBase &&r) noexcept = default;
	// inline ObjectBase &operator=(ObjectBase &&r) noexcept = default;
};

// Resource Base
enum class ResourceType { kImage, kBuffer };
enum class ResourceState { kManaged, kCombinedImage, kExternal };
enum class ResourceClass {
	kManagedImage,
	kExternalImageBase,
	kCombinedImage,
	kImageAlias,
	kManagedBuffer,
	kExternalBufferBase,
	kBufferAlias
};
class ResourceBase : virtual public ObjectBase {
private:
	ResourceClass m_class{};
	PassBase *m_producer_pass_ptr{};

	inline void set_producer_pass_ptr(PassBase *producer_pass_ptr) { m_producer_pass_ptr = producer_pass_ptr; }

	template <typename, typename...> friend class Pool;
	template <typename> friend class AliasOutputPool;

public:
	inline ~ResourceBase() override = default;
	inline ResourceBase() = default;
	inline ResourceBase(ResourceBase &&) noexcept = default;

	virtual ResourceType GetType() const = 0;
	virtual ResourceState GetState() const = 0;
	virtual bool IsAlias() const = 0;
	// virtual bool IsPerFrame() const = 0;
	// virtual void Resize(uint32_t width, uint32_t height) {}

	inline PassBase *GetProducerPassPtr() const { return m_producer_pass_ptr; }
};

#pragma endregion

//////////////////
// SECTION Pool //
//////////////////
#pragma region SECTION : Pool

// Pool Key
class PoolKey {
public:
	using LengthType = uint8_t;
	using IDType = uint16_t;
	inline constexpr static const std::size_t kMaxStrLen = 32 - sizeof(LengthType) - sizeof(IDType);

private:
	union {
		struct {
			IDType m_id;
			LengthType m_len;
			char m_str[kMaxStrLen];
		};
		std::tuple<uint64_t, uint64_t, uint64_t, uint64_t> _32_;
	};
	static_assert(sizeof(_32_) == 32);

public:
	inline PoolKey() : _32_{} {}
	template <typename IntType = IDType, typename = std::enable_if_t<std::is_integral_v<IntType>>>
	inline PoolKey(std::string_view str, IntType id) : m_str{}, m_len(std::min(str.length(), kMaxStrLen)), m_id(id) {
		std::copy(str.begin(), str.begin() + m_len, m_str);
	}
	inline PoolKey(std::string_view str)
	    : m_str{}, m_len(std::min(str.length(), kMaxStrLen)), m_id{std::numeric_limits<IDType>::max()} {
		std::copy(str.begin(), str.begin() + m_len, m_str);
	}
	inline PoolKey(const PoolKey &r) : _32_{r._32_} {}
	inline PoolKey &operator=(const PoolKey &r) {
		_32_ = r._32_;
		return *this;
	}
	inline std::string_view GetName() const { return std::string_view{m_str, m_len}; }
	inline IDType GetID() const { return m_id; }
	inline void SetName(std::string_view str) {
		m_len = std::min(str.length(), kMaxStrLen);
		std::copy(str.begin(), str.begin() + m_len, m_str);
		std::fill(m_str + m_len, m_str + kMaxStrLen, '\0');
	}
	inline void SetID(IDType id) { m_id = id; }

	inline bool operator<(const PoolKey &r) const { return _32_ < r._32_; }
	inline bool operator>(const PoolKey &r) const { return _32_ > r._32_; }
	inline bool operator==(const PoolKey &r) const { return _32_ == r._32_; }
	inline bool operator!=(const PoolKey &r) const { return _32_ != r._32_; }
	struct Hash {
		inline std::size_t operator()(PoolKey const &r) const noexcept {
			return std::get<0>(r._32_) ^ std::get<1>(r._32_) ^ std::get<2>(r._32_) ^ std::get<3>(r._32_);
			// return ((std::get<0>(r._32_) * 37 + std::get<1>(r._32_)) * 37 + std::get<2>(r._32_)) * 37 +
			//        std::get<3>(r._32_);
		}
	};
};
static_assert(sizeof(PoolKey) == 32 && std::is_move_constructible_v<PoolKey>);

// Pool
namespace _details_rg_pool_ {
template <typename Value> using PoolKeyMap = std::unordered_map<PoolKey, Value, PoolKey::Hash>;

template <typename Type> class TypeTraits {
private:
	inline constexpr static bool kIsObject = std::is_base_of_v<ObjectBase, Type>;
	inline constexpr static bool kAlterPtr =
	    (kIsObject && !std::is_final_v<Type>) || !std::is_move_constructible_v<Type>;
	inline constexpr static bool kAlterOptional = !kAlterPtr && (kIsObject || !std::is_default_constructible_v<Type>);

public:
	using AlterType = std::conditional_t<kAlterPtr, std::unique_ptr<Type>,
	                                     std::conditional_t<kAlterOptional, std::optional<Type>, Type>>;
	using VariantAlterType = std::conditional_t<kAlterPtr, std::unique_ptr<Type>, Type>;

	template <typename TypeToCons>
	inline constexpr static bool kCanConstruct =
	    kAlterPtr ? (std::is_base_of_v<Type, TypeToCons> || std::is_same_v<Type, TypeToCons>)
	              : std::is_same_v<Type, TypeToCons>;
	template <typename TypeToGet>
	inline constexpr static bool kCanGet =
	    kAlterPtr ? (std::is_base_of_v<Type, TypeToGet> || std::is_base_of_v<TypeToGet, Type> ||
	                 std::is_same_v<Type, TypeToGet>)
	              : (std::is_base_of_v<TypeToGet, Type> || std::is_same_v<Type, TypeToGet>);

	inline constexpr static bool kCanReset = kAlterPtr || kAlterOptional;

	template <typename TypeToCons, typename... Args, typename = std::enable_if_t<kCanConstruct<TypeToCons>>>
	inline static TypeToCons *Initialize(AlterType &val, Args &&...args) {
		if constexpr (kAlterPtr) {
			auto uptr = std::make_unique<TypeToCons>(std::forward<Args>(args)...);
			TypeToCons *ret = uptr.get();
			val = std::move(uptr);
			return ret;
		} else if constexpr (kAlterOptional) {
			val.emplace(std::forward<Args>(args)...);
			return &(val.value());
		} else {
			val = TypeToCons(std::forward<Args>(args)...);
			return &val;
		}
	}
	inline static void Reset(AlterType &val) {
		static_assert(kCanReset);
		val.reset();
	}
	inline static bool IsInitialized(const AlterType &val) {
		static_assert(kCanReset);
		if constexpr (kAlterPtr)
			return val;
		else if constexpr (kAlterOptional)
			return val.has_value();
		return true;
	}
	template <typename TypeToGet, typename = std::enable_if_t<kCanGet<TypeToGet>>>
	inline static TypeToGet *Get(const AlterType &val) {
		if constexpr (kAlterPtr) {
			if constexpr (std::is_same_v<Type, TypeToGet>)
				return (TypeToGet *)(val.get());
			else
				return (TypeToGet *)dynamic_cast<const TypeToGet *>(val.get());
		} else if constexpr (kAlterOptional) {
			if constexpr (std::is_same_v<Type, TypeToGet>)
				return val.has_value() ? (TypeToGet *)(&(val.value())) : nullptr;
			else
				return val.has_value() ? (TypeToGet *)dynamic_cast<const TypeToGet *>(&(val.value())) : nullptr;
		} else {
			if constexpr (std::is_same_v<Type, TypeToGet>)
				return (TypeToGet *)(&val);
			else
				return (TypeToGet *)dynamic_cast<const TypeToGet *>(&val);
		}
	}
};

template <class T> struct is_unique_ptr_impl : std::false_type {};
template <class T, class D> struct is_unique_ptr_impl<std::unique_ptr<T, D>> : std::true_type {};
template <class T> inline constexpr bool kIsUniquePtr = is_unique_ptr_impl<std::decay_t<T>>::value;

template <typename VariantType, typename TypeToCons, bool UniquePtr, size_t I = 0>
inline constexpr size_t GetVariantConstructIndex() {
	if constexpr (I >= std::variant_size_v<VariantType>) {
		return -1;
	} else {
		using VTI = std::variant_alternative_t<I, VariantType>;
		if constexpr (UniquePtr) {
			if constexpr (std::is_constructible_v<VTI, std::unique_ptr<TypeToCons> &&>)
				return I;
			else
				return (GetVariantConstructIndex<VariantType, TypeToCons, UniquePtr, I + 1>());
		} else {
			if constexpr (std::is_same_v<VTI, TypeToCons>)
				return I;
			else
				return (GetVariantConstructIndex<VariantType, TypeToCons, UniquePtr, I + 1>());
		}
	}
}
template <typename VariantType, typename TypeToCons, bool UniquePtr>
inline constexpr bool kVariantCanConstruct = GetVariantConstructIndex<VariantType, TypeToCons, UniquePtr>() != -1;

template <typename VariantType, typename TypeToGet, size_t I = 0> inline constexpr bool VariantCanGet() {
	if constexpr (I >= std::variant_size_v<VariantType>)
		return false;
	else {
		using VTI = std::variant_alternative_t<I, VariantType>;
		if constexpr (kIsUniquePtr<VTI>) {
			if constexpr (std::is_same_v<TypeToGet, typename std::pointer_traits<VTI>::element_type> ||
			              std::is_base_of_v<TypeToGet, typename std::pointer_traits<VTI>::element_type> ||
			              std::is_base_of_v<typename std::pointer_traits<VTI>::element_type, TypeToGet>)
				return true;
		} else {
			if constexpr (std::is_same_v<TypeToGet, VTI> || std::is_base_of_v<TypeToGet, VTI>)
				return true;
		}
		return VariantCanGet<VariantType, TypeToGet, I + 1>();
	}
}
template <typename TypeToGet> struct VariantGetter {
	template <typename ValType> inline TypeToGet *operator()(const ValType &val) {
		if constexpr (kIsUniquePtr<ValType>) {
			if constexpr (std::is_same_v<TypeToGet, typename std::pointer_traits<ValType>::element_type>)
				return (TypeToGet *)(val.get());
			else if constexpr (std::is_base_of_v<TypeToGet, typename std::pointer_traits<ValType>::element_type> ||
			                   std::is_base_of_v<typename std::pointer_traits<ValType>::element_type, TypeToGet>)
				return (TypeToGet *)dynamic_cast<const TypeToGet *>(val.get());
		} else {
			if constexpr (std::is_same_v<TypeToGet, ValType>)
				return (TypeToGet *)(&val);
			else if constexpr (std::is_base_of_v<TypeToGet, ValType>)
				return (TypeToGet *)dynamic_cast<const TypeToGet *>(&val);
		}
		return nullptr;
	}
};

template <typename... VariantArgs> struct TypeTraits<std::variant<VariantArgs...>> {
private:
	using Type = std::variant<VariantArgs...>;

public:
	using AlterType = Type;
	template <typename TypeToCons>
	inline constexpr static bool kCanConstruct =
	    kVariantCanConstruct<Type, TypeToCons, false> || kVariantCanConstruct<Type, TypeToCons, true>;
	template <typename TypeToGet> inline constexpr static bool kCanGet = VariantCanGet<Type, TypeToGet>();
	inline constexpr static bool kCanReset = true;

	template <typename TypeToCons, typename... Args, typename = std::enable_if_t<kCanConstruct<TypeToCons>>>
	inline static TypeToCons *Initialize(AlterType &val, Args &&...args) {
		if constexpr (kVariantCanConstruct<Type, TypeToCons, false>) {
			// If Don't need Pointer, prefer plain type
			constexpr size_t kIndex = GetVariantConstructIndex<Type, TypeToCons, false>();
			// printf("index = %lu\n", kIndex);
			val.template emplace<kIndex>(TypeToCons(std::forward<Args>(args)...));
			return &(std::get<kIndex>(val));
		} else {
			constexpr size_t kPtrIndex = GetVariantConstructIndex<Type, TypeToCons, true>();
			// printf("ptr_index = %lu\n", kPtrIndex);
			auto uptr = std::make_unique<TypeToCons>(std::forward<Args>(args)...);
			TypeToCons *ret = uptr.get();
			val.template emplace<kPtrIndex>(std::move(uptr));
			return ret;
		}
	}
	inline static void Reset(AlterType &val) { val = std::monostate{}; }
	inline static bool IsInitialized(const AlterType &val) { return val.index(); }
	template <typename TypeToGet, typename = std::enable_if_t<kCanGet<TypeToGet>>>
	inline static TypeToGet *Get(const AlterType &val) {
		return std::visit(VariantGetter<TypeToGet>{}, val);
	}
};
template <typename... Types> struct TypeTuple;
template <typename Type, typename... Others> struct TypeTuple<Type, Others...> {
	using T = decltype(std::tuple_cat(std::declval<std::tuple<typename TypeTraits<Type>::AlterType>>(),
	                                  std::declval<typename TypeTuple<Others...>::T>()));
};
template <> struct TypeTuple<> {
	using T = std::tuple<>;
};
template <typename, typename> struct VariantCat;
template <typename Type, typename... VariantTypes> struct VariantCat<Type, std::variant<VariantTypes...>> {
	using T = std::variant<VariantTypes..., Type>;
};
template <typename Type> struct VariantCat<Type, std::monostate> {
	using T = std::variant<std::monostate, Type>;
};
template <typename...> struct TypeVariant;
template <typename Type, typename... Others> struct TypeVariant<Type, Others...> {
	using T = typename VariantCat<typename TypeTraits<Type>::VariantAlterType, typename TypeVariant<Others...>::T>::T;
};
template <> struct TypeVariant<> {
	using T = std::monostate;
};

// Used for internal processing
template <typename... Types> struct PoolData {
	using TypeTuple = typename _details_rg_pool_::TypeTuple<Types...>::T;
	template <std::size_t Index> using GetRawType = std::tuple_element_t<Index, std::tuple<Types...>>;
	template <std::size_t Index> using GetAlterType = std::tuple_element_t<Index, TypeTuple>;
	template <std::size_t Index, typename T>
	inline static constexpr bool kCanConstruct =
	    _details_rg_pool_::TypeTraits<GetRawType<Index>>::template kCanConstruct<T>;
	template <std::size_t Index, typename T>
	inline static constexpr bool kCanGet = _details_rg_pool_::TypeTraits<GetRawType<Index>>::template kCanGet<T>;
	template <std::size_t Index>
	inline static constexpr bool kCanReset = _details_rg_pool_::TypeTraits<GetRawType<Index>>::kCanReset;

	_details_rg_pool_::PoolKeyMap<TypeTuple> pool;

	template <std::size_t Index, typename TypeToCons, typename... Args, typename MapIterator,
	          typename = std::enable_if_t<kCanConstruct<Index, TypeToCons>>>
	inline TypeToCons *ValueInitialize(const MapIterator &it, Args &&...args) {
		GetAlterType<Index> &ref = std::get<Index>(it->second);
		using RawType = GetRawType<Index>;
		return _details_rg_pool_::TypeTraits<RawType>::template Initialize<TypeToCons>(ref,
		                                                                               std::forward<Args>(args)...);
	}

	template <std::size_t Index, typename MapIterator, typename = std::enable_if_t<kCanReset<Index>>>
	inline bool ValueIsInitialized(const MapIterator &it) const {
		const GetAlterType<Index> &ref = std::get<Index>(it->second);
		using RawType = GetRawType<Index>;
		return _details_rg_pool_::TypeTraits<RawType>::IsInitialized(ref);
	}

	template <std::size_t Index, typename MapIterator, typename = std::enable_if_t<kCanReset<Index>>>
	inline void ValueReset(MapIterator &it) {
		GetAlterType<Index> &ref = std::get<Index>(it->second);
		using RawType = GetRawType<Index>;
		_details_rg_pool_::TypeTraits<RawType>::Reset(ref);
	}

	template <std::size_t Index, typename TypeToGet, typename MapIterator,
	          typename = std::enable_if_t<kCanGet<Index, TypeToGet>>>
	inline TypeToGet *ValueGet(const MapIterator &it) const {
		const GetAlterType<Index> &ref = std::get<Index>(it->second);
		using RawType = GetRawType<Index>;
		return _details_rg_pool_::TypeTraits<RawType>::template Get<TypeToGet>(ref);
	}

	inline PoolData() {
		static_assert(std::is_default_constructible_v<TypeTuple> && std::is_move_constructible_v<TypeTuple>);
		// TODO: Debug
		printf("%s\n", typeid(TypeTuple).name());
	}
	inline PoolData(PoolData &&) noexcept = default;
	inline ~PoolData() = default;
};

} // namespace _details_rg_pool_
template <typename... Types> using PoolVariant = typename _details_rg_pool_::TypeVariant<Types...>::T;

template <typename Derived, typename... Types> class Pool {
private:
	using PoolData = _details_rg_pool_::PoolData<Types...>;
	_details_rg_pool_::PoolData<Types...> m_data;

	template <std::size_t Index, typename TypeToCons, typename... Args, typename MapIterator>
	inline TypeToCons *initialize_and_set_rg_data(const MapIterator &it, Args &&...args) {
		TypeToCons *ptr = m_data.template ValueInitialize<Index, TypeToCons>(it, std::forward<Args>(args)...);
		// Initialize ObjectBase
		if constexpr (std::is_base_of_v<ObjectBase, TypeToCons>) {
			static_assert(std::is_base_of_v<RenderGraphBase, Derived> || std::is_base_of_v<ObjectBase, Derived>);

			auto base_ptr = static_cast<ObjectBase *>(ptr);
			base_ptr->set_key_ptr(&(it->first));
			if constexpr (std::is_base_of_v<RenderGraphBase, Derived>)
				base_ptr->set_render_graph_ptr((RenderGraphBase *)static_cast<const Derived *>(this));
			else
				base_ptr->set_render_graph_ptr(((ObjectBase *)static_cast<const Derived *>(this))->GetRenderGraphPtr());
		}
		// Initialize ResourceBase
		if constexpr (std::is_base_of_v<ResourceBase, TypeToCons>) {
			auto resource_ptr = static_cast<ResourceBase *>(ptr);
			if constexpr (std::is_base_of_v<PassBase, Derived>)
				resource_ptr->set_producer_pass_ptr((PassBase *)static_cast<const Derived *>(this));
		}
		return ptr;
	}

public:
	inline Pool() = default;
	inline Pool(Pool &&) noexcept = default;
	inline virtual ~Pool() = default;

protected:
	// Get PoolData Pointer
	// inline PoolData &GetPoolData() { return m_data; }
	inline const PoolData &GetPoolData() const { return m_data; }
	// Create Tag and Initialize the Main Object
	template <std::size_t Index, typename TypeToCons, typename... Args>
	inline TypeToCons *CreateAndInitialize(const PoolKey &key, Args &&...args) {
		// TODO: Delete this branch ?
		if (m_data.pool.find(key) != m_data.pool.end())
			return nullptr;
		auto it = m_data.pool.insert({key, typename PoolData::TypeTuple{}}).first;
		return initialize_and_set_rg_data<Index, TypeToCons, Args...>(it, std::forward<Args>(args)...);
	}
	// Create Tag Only
	inline void Create(const PoolKey &key) {
		if (m_data.pool.find(key) != m_data.pool.end())
			return;
		m_data.pool.insert(std::make_pair(key, typename PoolData::TypeTuple{}));
	}
	// Initialize Object of a Tag
	template <std::size_t Index, typename TypeToCons, typename... Args>
	inline TypeToCons *Initialize(const PoolKey &key, Args &&...args) {
		auto it = m_data.pool.find(key);
		return it == m_data.pool.end()
		           ? nullptr
		           : initialize_and_set_rg_data<Index, TypeToCons, Args...>(it, std::forward<Args>(args)...);
	}
	// Check whether an Object of a Tag is Initialized
	template <std::size_t Index> inline bool IsInitialized(const PoolKey &key) const {
		auto it = m_data.pool.find(key);
		return it != m_data.pool.end() && m_data.template ValueIsInitialized<Index>(it);
	}
	// Check whether a Tag exists
	inline bool Exist(const PoolKey &key) const { return m_data.pool.find(key) != m_data.pool.end(); }
	// Reset an Object of a Tag
	template <std::size_t Index> inline void Reset(const PoolKey &key) {
		auto it = m_data.pool.find(key);
		if (it != m_data.pool.end())
			m_data.template ValueReset<Index>(it);
	}
	// Get an Object from a Tag, if not Initialized, Initialize it.
	template <std::size_t Index, typename TypeToCons, typename... Args>
	inline TypeToCons *InitializeOrGet(const PoolKey &key, Args &&...args) {
		auto it = m_data.pool.find(key);
		if (it == m_data.pool.end())
			return nullptr;
		return m_data.template ValueIsInitialized<Index>(it)
		           ? (TypeToCons *)m_data.template ValueGet<Index, TypeToCons>(it)
		           : initialize_and_set_rg_data<Index, TypeToCons, Args...>(it, std::forward<Args>(args)...);
	}
	// Delete a Tag and its Objects
	inline void Delete(const PoolKey &key) { m_data.pool.erase(key); }
	// Get an Object from a Tag
	template <std::size_t Index, typename Type> inline Type *Get(const PoolKey &key) const {
		auto it = m_data.pool.find(key);
		return it == m_data.pool.end() ? nullptr : (Type *)m_data.template ValueGet<Index, Type>(it);
	}
	// Delete all Tags and Objects
	inline void Clear() { m_data.pool.clear(); }
};

#pragma endregion

////////////////////////
// SECTION: Resources //
////////////////////////
#pragma region SECTION : Resources

class BufferBase : virtual public ResourceBase {
public:
	inline ~BufferBase() override = default;
	inline BufferBase() = default;
	inline BufferBase(BufferBase &&) noexcept = default;

	inline ResourceType GetType() const final { return ResourceType::kBuffer; }
	virtual const myvk::Ptr<myvk::BufferBase> &GetVkBuffer() const = 0;
};

enum class AttachmentLoadOp { kClear, kLoad, kDontCare };
class ImageBase : virtual public ResourceBase {
public:
	inline ~ImageBase() override = default;
	inline ImageBase() = default;
	inline ImageBase(ImageBase &&) noexcept = default;

	inline ResourceType GetType() const final { return ResourceType::kImage; }
	virtual const myvk::Ptr<myvk::ImageView> &GetVkImageView() const = 0;

	virtual AttachmentLoadOp GetLoadOp() const = 0;
	virtual const VkClearValue &GetClearValue() const = 0;
};
// External
// TODO: External barriers (pipeline stage + access + image layout, begin & end)
class ExternalImageBase : virtual public ImageBase {
public:
	inline ExternalImageBase() = default;
	inline ExternalImageBase(ExternalImageBase &&) noexcept = default;
	inline ~ExternalImageBase() override = default;

	inline ResourceState GetState() const final { return ResourceState::kExternal; }
	bool IsAlias() const final { return false; }

private:
	AttachmentLoadOp m_load_op{AttachmentLoadOp::kDontCare};
	VkClearValue m_clear_value{};

public:
	template <AttachmentLoadOp LoadOp, typename = std::enable_if_t<LoadOp != AttachmentLoadOp::kClear>>
	inline void SetLoadOp() {
		m_load_op = LoadOp;
	}
	template <AttachmentLoadOp LoadOp, typename = std::enable_if_t<LoadOp == AttachmentLoadOp::kClear>>
	inline void SetLoadOp(const VkClearValue &clear_value) {
		m_load_op = LoadOp;
		m_clear_value = clear_value;
	}
	inline AttachmentLoadOp GetLoadOp() const final { return m_load_op; }
	inline const VkClearValue &GetClearValue() const final { return m_clear_value; }
};
class ExternalBufferBase : virtual public BufferBase {
public:
	inline ExternalBufferBase() = default;
	inline ExternalBufferBase(ExternalBufferBase &&) noexcept = default;
	inline ~ExternalBufferBase() override = default;

	inline ResourceState GetState() const final { return ResourceState::kExternal; }
	bool IsAlias() const final { return false; }
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
	ImageBase *m_pointed_image;

public:
	inline explicit ImageAlias(ImageBase *image)
	    : m_pointed_image{image->IsAlias() ? static_cast<ImageAlias *>(image)->GetPointedResource() : image} {}
	inline ImageAlias(ImageAlias &&) noexcept = default;
	inline ~ImageAlias() final = default;

	inline ImageBase *GetPointedResource() const { return m_pointed_image; }

	inline const myvk::Ptr<myvk::ImageView> &GetVkImageView() const final { return m_pointed_image->GetVkImageView(); }
	inline AttachmentLoadOp GetLoadOp() const final { return m_pointed_image->GetLoadOp(); }
	const VkClearValue &GetClearValue() const final { return m_pointed_image->GetClearValue(); }

	bool IsAlias() const final { return true; }
	ResourceState GetState() const final { return m_pointed_image->GetState(); }
};
class BufferAlias final : public BufferBase {
private:
	BufferBase *m_pointed_buffer;

public:
	inline explicit BufferAlias(BufferBase *buffer)
	    : m_pointed_buffer{buffer->IsAlias() ? static_cast<BufferAlias *>(buffer)->GetPointedResource() : buffer} {}
	inline BufferAlias(BufferAlias &&) = default;
	inline ~BufferAlias() final = default;

	inline BufferBase *GetPointedResource() const { return m_pointed_buffer; }

	inline const myvk::Ptr<myvk::BufferBase> &GetVkBuffer() const final { return m_pointed_buffer->GetVkBuffer(); }

	bool IsAlias() const final { return true; }
	ResourceState GetState() const final { return m_pointed_buffer->GetState(); }
};

// Managed Resources
// TODO: Complete this
class ManagedBuffer final : public BufferBase {
public:
	inline ManagedBuffer() = default;
	inline ManagedBuffer(ManagedBuffer &&) noexcept = default;
	~ManagedBuffer() override = default;

	const myvk::Ptr<myvk::BufferBase> &GetVkBuffer() const final {
		static myvk::Ptr<myvk::BufferBase> x;
		return x;
	}
	bool IsAlias() const final { return false; }
	ResourceState GetState() const final { return ResourceState::kManaged; }
};
class ManagedImage final : public ImageBase {
public:
	inline ManagedImage() = default;
	inline ManagedImage(ManagedImage &&) noexcept = default;
	~ManagedImage() override = default;

	const myvk::Ptr<myvk::ImageView> &GetVkImageView() const final {
		static myvk::Ptr<myvk::ImageView> x;
		return x;
	}
	bool IsAlias() const final { return false; }
	ResourceState GetState() const final { return ResourceState::kManaged; }

	inline AttachmentLoadOp GetLoadOp() const final { return {}; }
	const VkClearValue &GetClearValue() const final {
		static VkClearValue x;
		return x;
	}
};

// Resource Pool
namespace _details_rg_pool_ {
using ResourcePoolData = PoolData<PoolVariant<ManagedBuffer, ExternalBufferBase, ManagedImage, ExternalImageBase>>;
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

#pragma endregion

////////////////////////////
// SECTION Resource Usage //
////////////////////////////
#pragma region SECTION : Resource Usage

enum class Usage {
	kPreserveImage,
	kPreserveBuffer,
	kColorAttachmentW,
	kColorAttachmentRW,
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
	kTransferImageSrc,
	kTransferImageDst,
	kTransferBufferSrc,
	kTransferBufferDst,
	___USAGE_NUM
};
struct UsageInfo {
	VkAccessFlags2 read_access_flags, write_access_flags;

	ResourceType resource_type;
	VkFlags resource_creation_usages;
	VkImageLayout image_layout;

	VkPipelineStageFlags2 specified_pipeline_stages;
	VkPipelineStageFlags2 optional_pipeline_stages;

	bool is_descriptor;
	VkDescriptorType descriptor_type;
};
inline constexpr VkPipelineStageFlags2 __PIPELINE_STAGE_ALL_SHADERS_BIT =
    VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT |
    VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT | VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT |
    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

template <Usage> inline constexpr UsageInfo kUsageInfo{};
template <>
inline constexpr UsageInfo kUsageInfo<Usage::kPreserveImage> = {0, 0, ResourceType::kImage, 0, {}, 0, 0, false, {}};
template <>
inline constexpr UsageInfo kUsageInfo<Usage::kPreserveBuffer> = {0, 0, ResourceType::kBuffer, 0, {}, 0, 0, false, {}};
template <>
inline constexpr UsageInfo kUsageInfo<Usage::kColorAttachmentW> = {0,
                                                                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                                                   ResourceType::kImage,
                                                                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                                                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                                   VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                                                   0,
                                                                   false,
                                                                   {}};
template <>
inline constexpr UsageInfo kUsageInfo<Usage::kColorAttachmentRW> = {VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
                                                                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                                                    ResourceType::kImage,
                                                                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                                                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                                                    0,
                                                                    false,
                                                                    {}};
template <>
inline constexpr UsageInfo kUsageInfo<Usage::kDepthAttachmentR> = {VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                                                                   0,
                                                                   ResourceType::kImage,
                                                                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                                                   VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                                                   VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                                                                   0,
                                                                   false,
                                                                   {}};
template <>
inline constexpr UsageInfo kUsageInfo<Usage::kDepthAttachmentRW> = {VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                                                                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                                                    ResourceType::kImage,
                                                                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                                                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                                                    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                                                        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                                                                    0,
                                                                    false,
                                                                    {}};
template <>
inline constexpr UsageInfo kUsageInfo<Usage::kInputAttachment> = {VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT,
                                                                  0,
                                                                  ResourceType::kImage,
                                                                  VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
                                                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                                                  0,
                                                                  true,
                                                                  VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT};
// TODO: Is it correct?
template <>
inline constexpr UsageInfo kUsageInfo<Usage::kPresent> = {
    0, 0, ResourceType::kImage, 0, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 0, 0, false, {}};
template <>
inline constexpr UsageInfo kUsageInfo<Usage::kSampledImage> = {VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                                                               0,
                                                               ResourceType::kImage,
                                                               VK_IMAGE_USAGE_SAMPLED_BIT,
                                                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                               0,
                                                               __PIPELINE_STAGE_ALL_SHADERS_BIT,
                                                               true,
                                                               VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER};
template <>
inline constexpr UsageInfo kUsageInfo<Usage::kStorageImageR> = {VK_ACCESS_2_SHADER_STORAGE_READ_BIT, //
                                                                0,
                                                                ResourceType::kImage,
                                                                VK_IMAGE_USAGE_STORAGE_BIT,
                                                                VK_IMAGE_LAYOUT_GENERAL,
                                                                0,
                                                                __PIPELINE_STAGE_ALL_SHADERS_BIT,
                                                                true,
                                                                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
template <>
inline constexpr UsageInfo kUsageInfo<Usage::kStorageImageW> = {0,
                                                                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                                                                ResourceType::kImage,
                                                                VK_IMAGE_USAGE_STORAGE_BIT,
                                                                VK_IMAGE_LAYOUT_GENERAL,
                                                                0,
                                                                __PIPELINE_STAGE_ALL_SHADERS_BIT,
                                                                true,
                                                                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
template <>
inline constexpr UsageInfo kUsageInfo<Usage::kStorageImageRW> = {VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                                                                 VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                                                                 ResourceType::kImage,
                                                                 VK_IMAGE_USAGE_STORAGE_BIT,
                                                                 VK_IMAGE_LAYOUT_GENERAL,
                                                                 0,
                                                                 __PIPELINE_STAGE_ALL_SHADERS_BIT,
                                                                 true,
                                                                 VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
template <>
inline constexpr UsageInfo kUsageInfo<Usage::kUniformBuffer> = {VK_ACCESS_2_UNIFORM_READ_BIT, //
                                                                0,
                                                                ResourceType::kBuffer,
                                                                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                                {},
                                                                0,
                                                                __PIPELINE_STAGE_ALL_SHADERS_BIT,
                                                                true,
                                                                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER};
template <>
inline constexpr UsageInfo kUsageInfo<Usage::kStorageBufferR> = {VK_ACCESS_2_SHADER_STORAGE_READ_BIT, //
                                                                 0,
                                                                 ResourceType::kBuffer,
                                                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                                 {},
                                                                 0,
                                                                 __PIPELINE_STAGE_ALL_SHADERS_BIT,
                                                                 true,
                                                                 VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
template <>
inline constexpr UsageInfo kUsageInfo<Usage::kStorageBufferW> = {0, //
                                                                 VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                                                                 ResourceType::kBuffer,
                                                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                                 {},
                                                                 0,
                                                                 __PIPELINE_STAGE_ALL_SHADERS_BIT,
                                                                 true,
                                                                 VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
template <>
inline constexpr UsageInfo kUsageInfo<Usage::kStorageBufferRW> = {VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                                                                  VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                                                                  ResourceType::kBuffer,
                                                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                                  {},
                                                                  0,
                                                                  __PIPELINE_STAGE_ALL_SHADERS_BIT,
                                                                  true,
                                                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
template <>
inline constexpr UsageInfo kUsageInfo<Usage::kVertexBuffer> = {VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
                                                               0,
                                                               ResourceType::kBuffer,
                                                               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                                               {},
                                                               VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT,
                                                               0,
                                                               false,
                                                               {}};
template <>
inline constexpr UsageInfo kUsageInfo<Usage::kIndexBuffer> = {VK_ACCESS_2_INDEX_READ_BIT, //
                                                              0,
                                                              ResourceType::kBuffer,
                                                              VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                                              {},
                                                              VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT,
                                                              0,
                                                              false,
                                                              {}};
template <>
inline constexpr UsageInfo kUsageInfo<Usage::kTransferImageSrc> = {
    VK_ACCESS_2_TRANSFER_READ_BIT, //
    0,
    ResourceType::kImage,
    VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    0,
    VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT, // Copy or Blit as SRC
    false,
    {}};
template <>
inline constexpr UsageInfo kUsageInfo<Usage::kTransferImageDst> = {
    0, //
    VK_ACCESS_2_TRANSFER_WRITE_BIT,
    ResourceType::kImage,
    VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    0,
    VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT |
        VK_PIPELINE_STAGE_2_CLEAR_BIT, // Copy, Blit, Clear as DST
    false,
    {}};
template <>
inline constexpr UsageInfo kUsageInfo<Usage::kTransferBufferSrc> = {VK_ACCESS_2_TRANSFER_READ_BIT, //
                                                                    0,
                                                                    ResourceType::kBuffer,
                                                                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                                                    {},
                                                                    VK_PIPELINE_STAGE_2_COPY_BIT, // ONLY Copy as SRC
                                                                    0,
                                                                    false,
                                                                    {}};
template <>
inline constexpr UsageInfo kUsageInfo<Usage::kTransferBufferDst> = {
    0, //
    VK_ACCESS_2_TRANSFER_WRITE_BIT,
    ResourceType::kBuffer,
    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    {},
    0,
    VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT, // Copy or Fill as DST
    false,
    {}};
template <typename IndexSequence> class UsageInfoTable;
template <std::size_t... Indices> class UsageInfoTable<std::index_sequence<Indices...>> {
private:
	inline static constexpr UsageInfo kArr[] = {(kUsageInfo<(Usage)Indices>)...};

public:
	inline constexpr const UsageInfo &operator[](Usage usage) const { return kArr[(std::size_t)usage]; }
};
inline constexpr UsageInfoTable<std::make_index_sequence<(std::size_t)Usage::___USAGE_NUM>> kUsageInfoTable{};

// Is Descriptor
template <Usage Usage> inline constexpr bool kUsageIsDescriptor = kUsageInfo<Usage>.is_descriptor;
inline constexpr bool UsageIsDescriptor(Usage usage) { return kUsageInfoTable[usage].is_descriptor; }
// Get Descriptor Type
template <Usage Usage> inline constexpr VkDescriptorType kUsageGetDescriptorType = kUsageInfo<Usage>.descriptor_type;
inline constexpr VkDescriptorType UsageGetDescriptorType(Usage usage) { return kUsageInfoTable[usage].descriptor_type; }
// Get Read Access Flags
template <Usage Usage> inline constexpr VkAccessFlags2 kUsageGetReadAccessFlags = kUsageInfo<Usage>.read_access_flags;
inline constexpr VkAccessFlags2 UsageGetReadAccessFlags(Usage usage) {
	return kUsageInfoTable[usage].read_access_flags;
}
// Get Write Access Flags
template <Usage Usage> inline constexpr VkAccessFlags2 kUsageGetWriteAccessFlags = kUsageInfo<Usage>.write_access_flags;
inline constexpr VkAccessFlags2 UsageGetWriteAccessFlags(Usage usage) {
	return kUsageInfoTable[usage].write_access_flags;
}
// Get Access Flags
template <Usage Usage>
inline constexpr VkAccessFlags2 kUsageGetAccessFlags =
    kUsageGetReadAccessFlags<Usage> | kUsageGetWriteAccessFlags<Usage>;
inline constexpr VkAccessFlags2 UsageGetAccessFlags(Usage usage) {
	return UsageGetReadAccessFlags(usage) | UsageGetWriteAccessFlags(usage);
}
// Is Read-Only
template <Usage Usage> inline constexpr bool kUsageIsReadOnly = kUsageGetWriteAccessFlags<Usage> == 0;
inline constexpr bool UsageIsReadOnly(Usage usage) { return UsageGetWriteAccessFlags(usage) == 0; }
// Is For Buffer
template <Usage Usage> inline constexpr bool kUsageForBuffer = kUsageInfo<Usage>.resource_type == ResourceType::kBuffer;
inline constexpr bool UsageForBuffer(Usage usage) {
	return kUsageInfoTable[usage].resource_type == ResourceType::kBuffer;
}
// Is For Image
template <Usage Usage> inline constexpr bool kUsageForImage = kUsageInfo<Usage>.resource_type == ResourceType::kImage;
inline constexpr bool UsageForImage(Usage usage) {
	return kUsageInfoTable[usage].resource_type == ResourceType::kImage;
}
// Is Color Attachment
template <Usage Usage>
inline constexpr bool kUsageIsColorAttachment = Usage == Usage::kColorAttachmentW || Usage == Usage::kColorAttachmentRW;
inline constexpr bool UsageIsColorAttachment(Usage usage) {
	return usage == Usage::kColorAttachmentW || usage == Usage::kColorAttachmentRW;
}
// Is Depth Attachment
template <Usage Usage>
inline constexpr bool kUsageIsDepthAttachment = Usage == Usage::kDepthAttachmentR || Usage == Usage::kDepthAttachmentRW;
inline constexpr bool UsageIsDepthAttachment(Usage usage) {
	return usage == Usage::kDepthAttachmentR || usage == Usage::kDepthAttachmentRW;
}
// Is Attachment
template <Usage Usage>
inline constexpr bool kUsageIsAttachment =
    kUsageIsColorAttachment<Usage> || kUsageIsDepthAttachment<Usage> || Usage == Usage::kInputAttachment;
inline constexpr bool UsageIsAttachment(Usage usage) {
	return UsageIsColorAttachment(usage) || UsageIsDepthAttachment(usage) || usage == Usage::kInputAttachment;
}
// Has Specified Pipeline Stages
template <Usage Usage>
inline constexpr bool kUsageHasSpecifiedPipelineStages = kUsageInfo<Usage>.specified_pipeline_stages;
inline constexpr bool UsageHasSpecifiedPipelineStages(Usage usage) {
	return kUsageInfoTable[usage].specified_pipeline_stages;
}
// Get Specified Pipeline Stages
template <Usage Usage>
inline constexpr VkPipelineStageFlags2 kUsageGetSpecifiedPipelineStages = kUsageInfo<Usage>.specified_pipeline_stages;
inline constexpr VkPipelineStageFlags2 UsageGetSpecifiedPipelineStages(Usage usage) {
	return kUsageInfoTable[usage].specified_pipeline_stages;
}
// Get Optional Pipeline Stages
template <Usage Usage>
inline constexpr VkPipelineStageFlags2 kUsageGetOptionalPipelineStages = kUsageInfo<Usage>.optional_pipeline_stages;
inline constexpr VkPipelineStageFlags2 UsageGetOptionalPipelineStages(Usage usage) {
	return kUsageInfoTable[usage].optional_pipeline_stages;
}
// Get Image Layout
template <Usage Usage> inline constexpr VkImageLayout kUsageGetImageLayout = kUsageInfo<Usage>.image_layout;
inline constexpr VkImageLayout UsageGetImageLayout(Usage usage) { return kUsageInfoTable[usage].image_layout; }
// Get Resource Creation Usages
template <Usage Usage> inline constexpr VkFlags kUsageGetCreationUsages = kUsageInfo<Usage>.resource_creation_usages;
inline constexpr VkFlags UsageGetCreationUsages(Usage usage) { return kUsageInfoTable[usage].resource_creation_usages; }
// inline constexpr bool UsageAll(Usage) { return true; }

// Input Usage Operation
/*using UsageClass = bool(Usage);
namespace _details_rg_input_usage_op_ {
template <UsageClass... Args> struct Union;
template <UsageClass X, UsageClass... Args> struct Union<X, Args...> {
    constexpr bool operator()(Usage x) const { return X(x) || Union<Args...>(x); }
};
template <> struct Union<> {
    constexpr bool operator()(Usage) const { return false; }
};
template <bool... Args(Usage)> struct Intersect;
template <UsageClass X, UsageClass... Args> struct Intersect<X, Args...> {
    constexpr bool operator()(Usage x) const { return X(x) && Intersect<Args...>(x); }
};
template <> struct Intersect<> {
    constexpr bool operator()(Usage) const { return true; }
};
} // namespace _details_rg_input_usage_op_
// Union: A | B
template <UsageClass... Args> inline constexpr bool UsageUnion(Usage x) {
    return _details_rg_input_usage_op_::Union<Args...>(x);
}
// Intersect: A & B
template <UsageClass... Args> inline constexpr bool UsageIntersect(Usage x) {
    return _details_rg_input_usage_op_::Intersect<Args...>(x);
}
// Complement: ~X
template <UsageClass X> inline constexpr bool UsageComplement(Usage x) { return !X(x); }
// Minus: A & (~B)
template <UsageClass A, UsageClass B> inline constexpr bool UsageMinus(Usage x) {
    return UsageIntersect<A, UsageComplement<B>>(x);
}*/

#pragma endregion

//////////////////////////
// SECTION Resource I/O //
//////////////////////////
#pragma region SECTION : Resource IO

// Alias Output Pool (for Sub-pass)
template <typename Derived>
class AliasOutputPool : public Pool<Derived, BufferAlias>, public Pool<Derived, ImageAlias> {
private:
	template <typename Type, typename AliasType>
	inline Type *make_alias_output(const PoolKey &output_key, Type *resource) {
		using Pool = Pool<Derived, AliasType>;
		AliasType *alias = Pool ::template Get<0, AliasType>(output_key);
		if (alias == nullptr) {
			alias = Pool ::template CreateAndInitialize<0, AliasType>(output_key, resource);
		} else {
			if (alias->GetPointedResource() == resource)
				return alias;
			alias = Pool ::template Initialize<0, AliasType>(output_key, resource);
		}
		// Redirect ProducerPassPtr
		static_cast<ResourceBase *>(alias)->set_producer_pass_ptr(resource->GetProducerPassPtr());
		return alias;
	}

public:
	inline AliasOutputPool() = default;
	inline AliasOutputPool(AliasOutputPool &&) noexcept = default;
	inline virtual ~AliasOutputPool() = default;

protected:
	inline BufferBase *MakeBufferAliasOutput(const PoolKey &buffer_alias_output_key, BufferBase *buffer_output) {
		return make_alias_output<BufferBase, BufferAlias>(buffer_alias_output_key, buffer_output);
	}
	inline ImageBase *MakeImageAliasOutput(const PoolKey &image_alias_output_key, ImageBase *image_output) {
		return make_alias_output<ImageBase, ImageAlias>(image_alias_output_key, image_output);
	}
	// TODO: MakeCombinedImageOutput
	inline void RemoveBufferAliasOutput(const PoolKey &buffer_alias_output_key) {
		Pool<Derived, BufferAlias>::Delete(buffer_alias_output_key);
	}
	inline void RemoveImageAliasOutput(const PoolKey &image_alias_output_key) {
		Pool<Derived, ImageAlias>::Delete(image_alias_output_key);
	}
	inline void ClearBufferAliasOutputs() { Pool<Derived, BufferAlias>::Clear(); }
	inline void ClearImageAliasOutputs() { Pool<Derived, ImageAlias>::Clear(); }
};

// Resource Input
class Input {
private:
	std::variant<ImageBase *, BufferBase *> m_resource_ptr{};
	Usage m_usage{};
	VkPipelineStageFlags2 m_usage_pipeline_stages{};
	uint32_t m_descriptor_binding{UINT32_MAX};
	uint32_t m_attachment_index{UINT32_MAX};

public:
	inline Input() = default;
	template <typename Type>
	Input(Type *resource_ptr, Usage usage, VkPipelineStageFlags2 usage_pipeline_stages,
	      uint32_t descriptor_binding = UINT32_MAX, uint32_t attachment_index = UINT32_MAX)
	    : m_resource_ptr{resource_ptr}, m_usage{usage}, m_usage_pipeline_stages{usage_pipeline_stages},
	      m_descriptor_binding{descriptor_binding}, m_attachment_index{attachment_index} {}
	template <typename Type = ResourceBase> inline Type *GetResource() const {
		return std::visit(
		    [](auto arg) -> Type * {
			    using CURType = std::decay_t<decltype(*arg)>;
			    if constexpr (std::is_same_v<Type, CURType> || std::is_base_of_v<Type, CURType>)
				    return arg;
			    return nullptr;
		    },
		    m_resource_ptr);
	}
	inline Usage GetUsage() const { return m_usage; }
	inline VkPipelineStageFlags2 GetUsagePipelineStages() const { return m_usage_pipeline_stages; }
	inline uint32_t GetDescriptorBinding() const { return m_descriptor_binding; }
	inline uint32_t GetAttachmentIndex() const { return m_attachment_index; }
};

// Input Pool
namespace _details_rg_pool_ {
using InputPoolData = PoolData<Input, PoolVariant<BufferAlias, ImageAlias>>;
}
template <typename Derived> class InputPool : public Pool<Derived, Input, PoolVariant<BufferAlias, ImageAlias>> {
private:
	using _InputPool = Pool<Derived, Input, PoolVariant<BufferAlias, ImageAlias>>;

	template <typename... Args> inline Input *add_input(const PoolKey &input_key, Args &&...input_args) {
		return _InputPool::template CreateAndInitialize<0, Input>(input_key, std::forward<Args>(input_args)...);
	}

	template <typename Type, typename AliasType> inline Type *make_output(const PoolKey &input_key) {
		const Input *input = _InputPool::template Get<0, Input>(input_key);
		assert(input && !UsageIsReadOnly(input->GetUsage()));
		if (!input || UsageIsReadOnly(input->GetUsage())) // Read-Only input should not produce an output
			return nullptr;
		Type *resource = input->GetResource<Type>();
		assert(resource);
		if (!resource)
			return nullptr;
		else if (resource->GetProducerPassPtr() == (PassBase *)static_cast<Derived *>(this))
			return resource;
		else
			return _InputPool::template InitializeOrGet<1, AliasType>(input_key, resource);
	}

	template <typename> friend class DescriptorInputSlot;
	template <typename> friend class AttachmentInputSlot;

public:
	inline InputPool() { static_assert(std::is_base_of_v<PassBase, Derived>); }
	inline InputPool(InputPool &&) noexcept = default;
	inline ~InputPool() override = default;

protected:
	template <Usage Usage,
	          typename = std::enable_if_t<!kUsageIsAttachment<Usage> && !kUsageIsDescriptor<Usage> &&
	                                      kUsageHasSpecifiedPipelineStages<Usage> && kUsageForBuffer<Usage>>>
	inline bool AddInput(const PoolKey &input_key, BufferBase *buffer) {
		return add_input(input_key, buffer, Usage, kUsageGetSpecifiedPipelineStages<Usage>);
	}
	template <
	    Usage Usage, VkPipelineStageFlags2 PipelineStageFlags,
	    typename = std::enable_if_t<
	        !kUsageIsAttachment<Usage> && !kUsageIsDescriptor<Usage> && !kUsageHasSpecifiedPipelineStages<Usage> &&
	        (PipelineStageFlags & kUsageGetOptionalPipelineStages<Usage>) == PipelineStageFlags &&
	        kUsageForBuffer<Usage>>>
	inline bool AddInput(const PoolKey &input_key, BufferBase *buffer) {
		return add_input(input_key, buffer, Usage, PipelineStageFlags);
	}
	template <Usage Usage,
	          typename = std::enable_if_t<!kUsageIsAttachment<Usage> && !kUsageIsDescriptor<Usage> &&
	                                      kUsageHasSpecifiedPipelineStages<Usage> && kUsageForImage<Usage>>>
	inline bool AddInput(const PoolKey &input_key, ImageBase *image) {
		return add_input(input_key, image, Usage, kUsageGetSpecifiedPipelineStages<Usage>);
	}
	template <
	    Usage Usage, VkPipelineStageFlags2 PipelineStageFlags,
	    typename = std::enable_if_t<
	        !kUsageIsAttachment<Usage> && !kUsageIsDescriptor<Usage> && !kUsageHasSpecifiedPipelineStages<Usage> &&
	        (PipelineStageFlags & kUsageGetOptionalPipelineStages<Usage>) == PipelineStageFlags &&
	        kUsageForImage<Usage>>>
	inline bool AddInput(const PoolKey &input_key, ImageBase *image) {
		return add_input(input_key, image, Usage, PipelineStageFlags);
	}
	// TODO: AddCombinedImageInput()
	inline const Input *GetInput(const PoolKey &input_key) const {
		return _InputPool::template Get<0, Input>(input_key);
	}
	inline BufferBase *MakeBufferOutput(const PoolKey &input_buffer_key) {
		return make_output<BufferBase, BufferAlias>(input_buffer_key);
	}
	inline ImageBase *MakeImageOutput(const PoolKey &input_image_key) {
		return make_output<ImageBase, ImageAlias>(input_image_key);
	}
	inline void RemoveInput(const PoolKey &input_key);
	inline void ClearInputs();
};

class DescriptorBinding {
private:
	const Input *m_p_input{};
	myvk::Ptr<myvk::Sampler> m_sampler{};

public:
	inline DescriptorBinding() = default;
	inline explicit DescriptorBinding(const Input *input, const myvk::Ptr<myvk::Sampler> &sampler = nullptr)
	    : m_p_input{input}, m_sampler{sampler} {}
	inline void SetInput(const Input *input) { m_p_input = input; }
	inline void SetSampler(const myvk::Ptr<myvk::Sampler> &sampler) { m_sampler = sampler; }
	inline void Reset() {
		m_sampler.reset();
		m_p_input = nullptr;
	}
	inline const Input *GetInputPtr() const { return m_p_input; }
	inline const myvk::Ptr<myvk::Sampler> &GetVkSampler() const { return m_sampler; }
};

class DescriptorSetData : public ObjectBase {
private:
	std::unordered_map<uint32_t, DescriptorBinding> m_bindings;

	mutable myvk::Ptr<myvk::DescriptorSetLayout> m_descriptor_set_layout;
	mutable std::vector<myvk::Ptr<myvk::DescriptorSet>> m_descriptor_sets;
	mutable bool m_updated = true;

public:
	inline bool IsBindingExist(uint32_t binding) const { return m_bindings.find(binding) != m_bindings.end(); }
	inline void AddBinding(uint32_t binding, const Input *input, const myvk::Ptr<myvk::Sampler> &sampler = nullptr) {
		m_bindings.insert({binding, DescriptorBinding{input, sampler}});
		m_updated = true;
	}
	inline void RemoveBinding(uint32_t binding) {
		m_bindings.erase(binding);
		m_updated = true;
	}
	inline void ClearBindings() {
		m_bindings.clear();
		m_updated = true;
	}

	const myvk::Ptr<myvk::DescriptorSetLayout> &GetVkDescriptorSetLayout() const;
};

class AttachmentData {
private:
	std::vector<const Input *> m_color_attachments, m_input_attachments;
	const Input *m_depth_attachment{};

public:
	inline AttachmentData() {
		m_color_attachments.reserve(8);
		m_input_attachments.reserve(8);
	}
	inline bool IsColorAttachmentExist(uint32_t index) const {
		return index < m_color_attachments.size() && m_color_attachments[index];
	}
	inline bool IsInputAttachmentExist(uint32_t index) const {
		return index < m_input_attachments.size() && m_input_attachments[index];
	}
	inline bool IsDepthAttachmentExist() const { return m_depth_attachment; }
	inline void AddColorAttachment(uint32_t index, const Input *input) {
		if (m_color_attachments.size() <= index)
			m_color_attachments.resize(index + 1);
		m_color_attachments[index] = input;
	}
	inline void AddInputAttachment(uint32_t index, const Input *input) {
		if (m_input_attachments.size() <= index)
			m_input_attachments.resize(index + 1);
		m_input_attachments[index] = input;
	}
	inline void SetDepthAttachment(const Input *input) { m_depth_attachment = input; }
	inline void RemoveColorAttachment(uint32_t index) {
		m_color_attachments[index] = nullptr;
		while (!m_color_attachments.empty() && m_color_attachments.back() == nullptr)
			m_color_attachments.pop_back();
	}
	inline void RemoveInputAttachment(uint32_t index) {
		m_input_attachments[index] = nullptr;
		while (!m_input_attachments.empty() && m_input_attachments.back() == nullptr)
			m_input_attachments.pop_back();
	}
	inline void ResetDepthAttachment() { m_depth_attachment = nullptr; }
	inline void ClearAttachmens() {
		m_color_attachments.clear();
		m_input_attachments.clear();
		m_depth_attachment = nullptr;
	}
};

template <typename Derived> class DescriptorInputSlot {
private:
	DescriptorSetData m_descriptor_set_data;

	inline InputPool<Derived> *get_input_pool_ptr() {
		static_assert(std::is_base_of_v<InputPool<Derived>, Derived>);
		return (InputPool<Derived> *)static_cast<Derived *>(this);
	}
	inline const InputPool<Derived> *get_input_pool_ptr() const {
		static_assert(std::is_base_of_v<InputPool<Derived>, Derived>);
		return (const InputPool<Derived> *)static_cast<const Derived *>(this);
	}

	template <typename Type>
	inline Input *add_input_descriptor(const PoolKey &input_key, Type *resource, Usage usage,
	                                   VkPipelineStageFlags2 pipeline_stage_flags, uint32_t binding,
	                                   const myvk::Ptr<myvk::Sampler> &sampler = nullptr,
	                                   uint32_t attachment_index = UINT32_MAX) {
		assert(!m_descriptor_set_data.IsBindingExist(binding));
		if (m_descriptor_set_data.IsBindingExist(binding))
			return nullptr;
		auto input = get_input_pool_ptr()->add_input(input_key, resource, usage, pipeline_stage_flags, binding,
		                                             attachment_index);
		assert(input);
		if (!input)
			return nullptr;
		m_descriptor_set_data.AddBinding(binding, input, sampler);
		return input;
	}

	inline void pre_remove_input(const Input *input) {
		if (UsageIsDescriptor(input->GetUsage())) {
			assert(~(input->GetDescriptorBinding()));
			m_descriptor_set_data.RemoveBinding(input->GetDescriptorBinding());
		}
	}
	inline void pre_clear_inputs() { m_descriptor_set_data.ClearBindings(); }

	template <typename> friend class InputPool;
	template <typename> friend class AttachmentInputSlot;

public:
	inline DescriptorInputSlot() {
		static_assert(std::is_base_of_v<ObjectBase, Derived> || std::is_base_of_v<RenderGraphBase, Derived>);
		if constexpr (std::is_base_of_v<ObjectBase, Derived>)
			m_descriptor_set_data.set_render_graph_ptr(
			    ((ObjectBase *)static_cast<Derived *>(this))->GetRenderGraphPtr());
		else
			m_descriptor_set_data.set_render_graph_ptr((RenderGraphBase *)static_cast<Derived *>(this));
	}
	inline DescriptorInputSlot(DescriptorInputSlot &&) noexcept = default;
	inline ~DescriptorInputSlot() = default;

protected:
	inline const DescriptorSetData &GetDescriptorSetData() const { return m_descriptor_set_data; }

	template <uint32_t Binding, Usage Usage,
	          typename = std::enable_if_t<!kUsageIsAttachment<Usage> && kUsageIsDescriptor<Usage> &&
	                                      kUsageHasSpecifiedPipelineStages<Usage> && kUsageForBuffer<Usage>>>
	inline bool AddDescriptorInput(const PoolKey &input_key, BufferBase *buffer) {
		return add_input_descriptor(input_key, buffer, Usage, kUsageGetSpecifiedPipelineStages<Usage>, Binding);
	}
	template <uint32_t Binding, Usage Usage, VkPipelineStageFlags2 PipelineStageFlags,
	          typename = std::enable_if_t<
	              !kUsageIsAttachment<Usage> && kUsageIsDescriptor<Usage> && !kUsageHasSpecifiedPipelineStages<Usage> &&
	              (PipelineStageFlags & kUsageGetOptionalPipelineStages<Usage>) == PipelineStageFlags &&
	              kUsageForBuffer<Usage>>>
	inline bool AddDescriptorInput(const PoolKey &input_key, BufferBase *buffer) {
		return add_input_descriptor(input_key, buffer, Usage, PipelineStageFlags, Binding);
	}
	template <uint32_t Binding, Usage Usage,
	          typename = std::enable_if_t<!kUsageIsAttachment<Usage> && Usage != Usage::kSampledImage &&
	                                      kUsageIsDescriptor<Usage> && kUsageHasSpecifiedPipelineStages<Usage> &&
	                                      kUsageForImage<Usage>>>
	inline bool AddDescriptorInput(const PoolKey &input_key, ImageBase *image) {
		return add_input_descriptor(input_key, image, Usage, kUsageGetSpecifiedPipelineStages<Usage>, Binding);
	}
	template <uint32_t Binding, Usage Usage, VkPipelineStageFlags2 PipelineStageFlags,
	          typename = std::enable_if_t<!kUsageIsAttachment<Usage> && Usage != Usage::kSampledImage &&
	                                      kUsageIsDescriptor<Usage> && !kUsageHasSpecifiedPipelineStages<Usage> &&
	                                      (PipelineStageFlags & kUsageGetOptionalPipelineStages<Usage>) ==
	                                          PipelineStageFlags &&
	                                      kUsageForImage<Usage>>>
	inline bool AddDescriptorInput(const PoolKey &input_key, ImageBase *image) {
		return add_input_descriptor(input_key, image, Usage, PipelineStageFlags, Binding);
	}
	template <uint32_t Binding, Usage Usage,
	          typename = std::enable_if_t<Usage == Usage::kSampledImage && kUsageHasSpecifiedPipelineStages<Usage> &&
	                                      kUsageForImage<Usage>>>
	inline bool AddDescriptorInput(const PoolKey &input_key, ImageBase *image,
	                               const myvk::Ptr<myvk::Sampler> &sampler) {
		return add_input_descriptor(input_key, image, Usage, kUsageGetSpecifiedPipelineStages<Usage>, Binding, sampler);
	}
	template <uint32_t Binding, Usage Usage, VkPipelineStageFlags2 PipelineStageFlags,
	          typename = std::enable_if_t<Usage == Usage::kSampledImage && !kUsageHasSpecifiedPipelineStages<Usage> &&
	                                      (PipelineStageFlags & kUsageGetOptionalPipelineStages<Usage>) ==
	                                          PipelineStageFlags &&
	                                      kUsageForImage<Usage>>>
	inline bool AddDescriptorInput(const PoolKey &input_key, ImageBase *image,
	                               const myvk::Ptr<myvk::Sampler> &sampler) {
		return add_input_descriptor(input_key, image, Usage, PipelineStageFlags, Binding, sampler);
	}
	inline const myvk::Ptr<myvk::DescriptorSetLayout> &GetDescriptorSetLayout() const {
		return m_descriptor_set_data.GetVkDescriptorSetLayout();
	}
};

template <typename Derived> class AttachmentInputSlot {
private:
	AttachmentData m_attachment_data;

	inline InputPool<Derived> *get_input_pool_ptr() {
		static_assert(std::is_base_of_v<InputPool<Derived>, Derived>);
		return (InputPool<Derived> *)static_cast<Derived *>(this);
	}
	inline const InputPool<Derived> *get_input_pool_ptr() const {
		static_assert(std::is_base_of_v<InputPool<Derived>, Derived>);
		return (const InputPool<Derived> *)static_cast<const Derived *>(this);
	}

	inline DescriptorInputSlot<Derived> *get_descriptor_slot_ptr() {
		static_assert(std::is_base_of_v<DescriptorInputSlot<Derived>, Derived>);
		return (DescriptorInputSlot<Derived> *)static_cast<Derived *>(this);
	}
	inline const DescriptorInputSlot<Derived> *get_descriptor_slot_ptr() const {
		static_assert(std::is_base_of_v<DescriptorInputSlot<Derived>, Derived>);
		return (const DescriptorInputSlot<Derived> *)static_cast<const Derived *>(this);
	}

	inline void pre_remove_input(const Input *input) {
		if (UsageIsDepthAttachment(input->GetUsage()))
			m_attachment_data.ResetDepthAttachment();
		else if (UsageIsColorAttachment(input->GetUsage())) {
			assert(~(input->GetAttachmentIndex()));
			m_attachment_data.RemoveColorAttachment(input->GetAttachmentIndex());
		} else if (input->GetUsage() == Usage::kInputAttachment) {
			assert(~(input->GetAttachmentIndex()));
			m_attachment_data.RemoveInputAttachment(input->GetAttachmentIndex());
		}
	}
	inline void pre_clear_inputs() { m_attachment_data.ClearAttachmens(); }

	template <typename> friend class InputPool;

public:
	inline AttachmentInputSlot() {
		// TODO: Should AttachmentData be Object ?
		/* static_assert(std::is_base_of_v<ObjectBase, Derived> || std::is_base_of_v<RenderGraphBase, Derived>);
		if constexpr (std::is_base_of_v<ObjectBase, Derived>)
		    m_descriptor_set_data.set_render_graph_ptr(
		        ((ObjectBase *)static_cast<Derived *>(this))->GetRenderGraphPtr());
		else
		    m_descriptor_set_data.set_render_graph_ptr((RenderGraphBase *)static_cast<Derived *>(this)); */
	}
	inline AttachmentInputSlot(AttachmentInputSlot &&) noexcept = default;
	inline ~AttachmentInputSlot() = default;

protected:
	inline const AttachmentData &GetAttachmentData() const { return m_attachment_data; }

	template <uint32_t Index, Usage Usage, typename = std::enable_if_t<kUsageIsColorAttachment<Usage>>>
	inline bool AddColorAttachmentInput(const PoolKey &input_key, ImageBase *image) {
		static_assert(kUsageHasSpecifiedPipelineStages<Usage>);

		assert(!m_attachment_data.IsColorAttachmentExist(Index));
		if (m_attachment_data.IsColorAttachmentExist(Index))
			return false;
		auto input = get_input_pool_ptr()->add_input(input_key, image, Usage, kUsageGetSpecifiedPipelineStages<Usage>,
		                                             UINT32_MAX, Index);
		assert(input);
		if (!input)
			return false;
		m_attachment_data.AddColorAttachment(Index, input);
		return true;
	}

	template <uint32_t AttachmentIndex, uint32_t DescriptorBinding>
	inline bool AddInputAttachmentInput(const PoolKey &input_key, ImageBase *image) {
		static_assert(kUsageHasSpecifiedPipelineStages<Usage::kInputAttachment>);

		assert(!m_attachment_data.IsInputAttachmentExist(AttachmentIndex));
		if (m_attachment_data.IsInputAttachmentExist(AttachmentIndex))
			return false;
		auto input = get_descriptor_slot_ptr()->add_input_descriptor(
		    input_key, image, Usage::kInputAttachment, kUsageGetSpecifiedPipelineStages<Usage::kInputAttachment>,
		    DescriptorBinding, nullptr, AttachmentIndex);
		assert(input);
		if (!input)
			return false;
		m_attachment_data.AddInputAttachment(AttachmentIndex, input);
		return true;
	}

	template <Usage Usage, typename = std::enable_if_t<kUsageIsDepthAttachment<Usage>>>
	inline bool SetDepthAttachmentInput(const PoolKey &input_key, ImageBase *image) {
		static_assert(kUsageHasSpecifiedPipelineStages<Usage>);

		assert(!m_attachment_data.IsDepthAttachmentExist());
		if (m_attachment_data.IsDepthAttachmentExist())
			return false;
		auto input = get_input_pool_ptr()->add_input(input_key, image, Usage, kUsageGetSpecifiedPipelineStages<Usage>);
		assert(input);
		if (!input)
			return false;
		m_attachment_data.SetDepthAttachment(input);
		return true;
	}
};

template <typename Derived> void InputPool<Derived>::RemoveInput(const PoolKey &input_key) {
	if constexpr (std::is_base_of_v<DescriptorInputSlot<Derived>, Derived> ||
	              std::is_base_of_v<AttachmentInputSlot<Derived>, Derived>) {
		const Input *input = GetInput(input_key);
		if constexpr (std::is_base_of_v<DescriptorInputSlot<Derived>, Derived>)
			((DescriptorInputSlot<Derived> *)static_cast<Derived *>(this))->pre_remove_input(input);
		if constexpr (std::is_base_of_v<AttachmentInputSlot<Derived>, Derived>)
			((AttachmentInputSlot<Derived> *)static_cast<Derived *>(this))->pre_remove_input(input);
	}
	InputPool::Delete(input_key);
}

template <typename Derived> void InputPool<Derived>::ClearInputs() {
	if constexpr (std::is_base_of_v<DescriptorInputSlot<Derived>, Derived>)
		((DescriptorInputSlot<Derived> *)static_cast<Derived *>(this))->pre_clear_inputs();
	if constexpr (std::is_base_of_v<AttachmentInputSlot<Derived>, Derived>)
		((AttachmentInputSlot<Derived> *)static_cast<Derived *>(this))->pre_clear_inputs();
	InputPool::Clear();
}

#pragma endregion

/////////////////////////
// SECTION Render Pass //
/////////////////////////
#pragma region SECTION : Render Pass

namespace _details_rg_pool_ {
using PassPoolData = PoolData<PassBase>;
}

class PassBase : public ObjectBase {
private:
	// PassGroup
	const std::vector<PassBase *> *m_p_pass_pool_sequence{};

	// Pass
	const _details_rg_pool_::InputPoolData *m_p_input_pool_data{};
	const DescriptorSetData *m_p_descriptor_set_data{};
	const AttachmentData *m_p_attachment_data{};
	// const _details_rg_pool_::ResourcePoolData *m_p_resource_pool_data{};

	mutable struct {
		uint32_t index;
		bool visited;
		// uint32_t in_degree;
	} m_traversal_data{};

	template <typename, uint8_t, bool> friend class Pass;
	template <typename, bool> friend class PassGroup;
	template <typename, bool> friend class GraphicsPass;
	friend class RenderGraphBase;

public:
	inline PassBase() = default;
	inline PassBase(PassBase &&) noexcept = default;
	inline ~PassBase() override = default;

	inline bool IsPassGroup() const { return m_p_pass_pool_sequence; }

	virtual void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) = 0;
};

template <typename Derived> class PassPool : public Pool<Derived, PassBase> {
private:
	using _PassPool = Pool<Derived, PassBase>;
	std::vector<PassBase *> m_pass_sequence;

public:
	inline PassPool() = default;
	inline PassPool(PassPool &&) noexcept = default;
	inline ~PassPool() override = default;

protected:
	template <typename PassType, typename... Args, typename = std::enable_if_t<std::is_base_of_v<PassBase, PassType>>>
	inline PassType *PushPass(const PoolKey &pass_key, Args &&...args) {
		PassType *ret =
		    _PassPool::template CreateAndInitialize<0, PassType, Args...>(pass_key, std::forward<Args>(args)...);
		assert(ret);
		m_pass_sequence.push_back(ret);
		return ret;
	}
	// inline void DeletePass(const PoolKey &pass_key) { return PassPool::Delete(pass_key); }

	const std::vector<PassBase *> &GetPassSequence() const { return m_pass_sequence; }

	template <typename PassType = PassBase,
	          typename = std::enable_if_t<std::is_base_of_v<PassBase, PassType> || std::is_same_v<PassBase, PassType>>>
	inline PassType *GetPass(const PoolKey &pass_key) const {
		return _PassPool::template Get<0, PassType>(pass_key);
	}
	inline void ClearPasses() {
		m_pass_sequence.clear();
		_PassPool::Clear();
	}
};

namespace _details_rg_pass_ {
struct NoResourcePool {};
struct NoPassPool {};
struct NoAliasOutputPool {};
struct NoDescriptorInputSlot {};
struct NoAttachmentInputSlot {};
} // namespace _details_rg_pass_

struct PassFlag {
	enum : uint8_t { kDescriptor = 4u, kGraphics = 8u, kCompute = 16u };
};

template <typename Derived, uint8_t Flags, bool EnableResource = false>
class Pass : public PassBase,
             public InputPool<Derived>,
             public std::conditional_t<EnableResource, ResourcePool<Derived>, _details_rg_pass_::NoResourcePool>,
             public std::conditional_t<(Flags & PassFlag::kDescriptor) != 0, DescriptorInputSlot<Derived>,
                                       _details_rg_pass_::NoDescriptorInputSlot>,
             public std::conditional_t<(Flags & PassFlag::kGraphics) != 0, AttachmentInputSlot<Derived>,
                                       _details_rg_pass_::NoAttachmentInputSlot> {
public:
	inline Pass() {
		m_p_input_pool_data = &InputPool<Derived>::GetPoolData();
		/* if constexpr ((Flags & PassFlag::kEnableResourceAllocation) != 0)
		    m_p_resource_pool_data = &ResourcePool<Derived>::GetPoolData();
		if constexpr ((Flags & PassFlag::kEnableSubpass) != 0)
		    m_p_pass_pool_data = &PassPool<Derived>::GetPoolData(); */
		if constexpr ((Flags & PassFlag::kDescriptor) != 0)
			m_p_descriptor_set_data = &DescriptorInputSlot<Derived>::GetDescriptorSetData();
		if constexpr ((Flags & PassFlag::kGraphics) != 0)
			m_p_attachment_data = &AttachmentInputSlot<Derived>::GetAttachmentData();
	}
	inline Pass(Pass &&) noexcept = default;
	inline ~Pass() override = default;
};

template <typename Derived, bool EnableResource = false>
class PassGroup : public PassBase,
                  public std::conditional_t<EnableResource, ResourcePool<Derived>, _details_rg_pass_::NoResourcePool>,
                  public PassPool<Derived>,
                  public AliasOutputPool<Derived> {
public:
	void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &) final {}

	inline PassGroup() { m_p_pass_pool_sequence = &PassPool<Derived>::GetPassSequence(); }
	inline PassGroup(PassGroup &&) noexcept = default;
	inline ~PassGroup() override = default;
};

#pragma endregion

//////////////////////////
// SECTION Render Graph //
//////////////////////////
#pragma region SECTION : Render Graph

namespace _details_rg_pool_ {
using ResultPoolData = PoolData<ResourceBase *>;
}
template <typename Derived> class ResultPool : public Pool<Derived, ResourceBase *> {
private:
	using _ResultPool = Pool<Derived, ResourceBase *>;

public:
	inline ResultPool() = default;
	inline ResultPool(ResultPool &&) noexcept = default;
	inline ~ResultPool() override = default;

protected:
	inline bool AddResult(const PoolKey &result_key, ResourceBase *resource) {
		assert(resource);
		return _ResultPool::template CreateAndInitialize<0, ResourceBase *>(result_key, resource);
	}
	inline bool IsResultExist(const PoolKey &result_key) const { return _ResultPool::Exist(result_key); }
	inline void RemoveResult(const PoolKey &result_key) { _ResultPool::Delete(result_key); }
	inline void ClearResults() { _ResultPool::Clear(); }
};

class RenderGraphBase : public myvk::DeviceObjectBase {
private:
	myvk::Ptr<myvk::Device> m_device_ptr;
	uint32_t m_frame_count{};
	VkExtent2D m_canvas_size{};

	const std::vector<PassBase *> *m_p_pass_pool_sequence{};
	const _details_rg_pool_::ResultPoolData *m_p_result_pool_data{};
	// const _details_rg_pool_::ResourcePoolData *m_p_resource_pool_data{};
	// const _details_rg_pool_::PassPoolData *m_p_pass_pool_data{};

	mutable std::vector<PassBase *> m_pass_sequence;
	void _visit_pass_graph(PassBase *pass) const;
	void _extract_visited_pass(const std::vector<PassBase *> *p_cur_seq) const;

	bool m_pass_graph_updated = true, m_resource_updated = true;

	template <typename> friend class RenderGraph;

protected:
	inline void SetFrameCount(uint32_t frame_count) {
		if (m_frame_count != frame_count)
			m_resource_updated = true;
		m_frame_count = frame_count;
	}
	inline void SetCanvasSize(const VkExtent2D &canvas_size) {}

public:
	inline const myvk::Ptr<myvk::Device> &GetDevicePtr() const final { return m_device_ptr; }
	inline uint32_t GetFrameCount() const { return m_frame_count; }
	void gen_pass_sequence() const;
};

template <typename Derived>
class RenderGraph : public RenderGraphBase,
                    public PassPool<Derived>,
                    public ResourcePool<Derived>,
                    public ResultPool<Derived> {
public:
	template <typename... Args>
	inline static myvk::Ptr<Derived> Create(const myvk::Ptr<myvk::Device> &device_ptr, Args &&...args) {
		static_assert(std::is_base_of_v<RenderGraph<Derived>, Derived>);

		auto ret = std::make_shared<Derived>(std::forward<Args>(args)...);
		dynamic_cast<RenderGraphBase *>(ret.get())->m_device_ptr = device_ptr;
		return ret;
	}
	inline RenderGraph() {
		m_p_result_pool_data = &ResultPool<Derived>::GetPoolData();
		m_p_pass_pool_sequence = &PassPool<Derived>::GetPassSequence();
		// m_p_resource_pool_data = &ResourcePool<Derived>::GetPoolData();
	}
};

#pragma endregion

// TODO: Debug Type Traits
static_assert(std::is_same_v<PoolVariant<BufferAlias, ObjectBase, ResourceBase, ImageAlias>,
                             std::variant<std::monostate, ImageAlias, std::unique_ptr<ResourceBase>,
                                          std::unique_ptr<ObjectBase>, BufferAlias>>);
} // namespace myvk_rg

#endif
