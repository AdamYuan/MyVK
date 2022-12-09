#ifndef MYVK_RENDER_GRAPH_HPP
#define MYVK_RENDER_GRAPH_HPP

#include "CommandBuffer.hpp"
#include "FrameManager.hpp"
#include "RenderGraphScheduler.hpp"
#include <optional>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>

namespace myvk::render_graph {
///////////////////////////
// SECTION: Base Objects //
///////////////////////////
#pragma region SECTION : Base Objects

class RenderGraphBase {};
class RGPassBase;
// Object Base
class RGObjectPoolKey;
class RGObjectBase {
private:
	RenderGraphBase *m_render_graph_ptr{};
	const RGObjectPoolKey *m_key_ptr{};

	inline void set_render_graph_ptr(RenderGraphBase *render_graph_ptr) { m_render_graph_ptr = render_graph_ptr; }
	inline void set_key_ptr(const RGObjectPoolKey *key_ptr) { m_key_ptr = key_ptr; }

	template <typename, typename...> friend class RGObjectPool;

public:
	inline RGObjectBase() = default;
	inline virtual ~RGObjectBase() = default;

	inline RenderGraphBase *GetRenderGraphPtr() const { return m_render_graph_ptr; }
	inline const RGObjectPoolKey &GetKey() const { return *m_key_ptr; }

	// Disable Copy
	inline RGObjectBase(RGObjectBase &&r) noexcept = default;
	// inline RGObjectBase &operator=(RGObjectBase &&r) noexcept = default;
};

// Resource Base
enum class RGResourceType { kImage, kBuffer };
enum class RGResourceState { kManaged, kExternal };
class RGResourceBase : virtual public RGObjectBase {
private:
	RGPassBase *m_producer_pass_ptr{};

	inline void set_producer_pass_ptr(RGPassBase *producer_pass_ptr) { m_producer_pass_ptr = producer_pass_ptr; }

	template <typename, typename...> friend class RGObjectPool;

public:
	inline ~RGResourceBase() override = default;
	inline RGResourceBase() = default;
	inline RGResourceBase(RGResourceBase &&) noexcept = default;

	virtual RGResourceType GetType() const = 0;
	virtual RGResourceState GetState() const = 0;
	virtual bool IsAlias() const = 0;
	// virtual bool IsPerFrame() const = 0;
	// virtual void Resize(uint32_t width, uint32_t height) {}

	inline RGPassBase *GetProducerPassPtr() const { return m_producer_pass_ptr; }
};

#pragma endregion

/////////////////////////
// SECTION Object Pool //
/////////////////////////
#pragma region SECTION : Object Pool

// Object Pool Key
class RGObjectPoolKey {
public:
	using LengthType = uint8_t;
	using IDType = uint16_t;
	constexpr static const std::size_t kMaxStrLen = 32 - sizeof(LengthType) - sizeof(IDType);

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
	inline RGObjectPoolKey() : _32_{} {}
	template <typename IntType = IDType, typename = std::enable_if_t<std::is_integral_v<IntType>>>
	inline RGObjectPoolKey(std::string_view str, IntType id = 0)
	    : m_str{}, m_len(std::min(str.length(), kMaxStrLen)), m_id(id) {
		std::copy(str.begin(), str.begin() + m_len, m_str);
	}
	inline RGObjectPoolKey(const RGObjectPoolKey &r) : _32_{r._32_} {}
	inline RGObjectPoolKey &operator=(const RGObjectPoolKey &r) {
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

	inline bool operator<(const RGObjectPoolKey &r) const { return _32_ < r._32_; }
	inline bool operator>(const RGObjectPoolKey &r) const { return _32_ > r._32_; }
	inline bool operator==(const RGObjectPoolKey &r) const { return _32_ == r._32_; }
	inline bool operator!=(const RGObjectPoolKey &r) const { return _32_ != r._32_; }
	struct Hash {
		inline std::size_t operator()(RGObjectPoolKey const &r) const noexcept {
			return std::get<0>(r._32_) ^ std::get<1>(r._32_) ^ std::get<2>(r._32_) ^ std::get<3>(r._32_);
			// return ((std::get<0>(r._32_) * 37 + std::get<1>(r._32_)) * 37 + std::get<2>(r._32_)) * 37 +
			//        std::get<3>(r._32_);
		}
	};
};
static_assert(sizeof(RGObjectPoolKey) == 32 && std::is_move_constructible_v<RGObjectPoolKey>);
template <typename Value> using RGKeyMap = std::unordered_map<RGObjectPoolKey, Value, RGObjectPoolKey::Hash>;

// Object Pool
namespace _details_rg_object_pool_ {

template <typename Type> class TypeTraits {
private:
	constexpr static bool kIsRGObject = std::is_base_of_v<RGObjectBase, Type>;
	constexpr static bool kAlterPtr = (kIsRGObject && !std::is_final_v<Type>) || !std::is_move_constructible_v<Type>;
	constexpr static bool kAlterOptional = !kAlterPtr && (kIsRGObject || !std::is_default_constructible_v<Type>);

public:
	using AlterType = std::conditional_t<kAlterPtr, std::unique_ptr<Type>,
	                                     std::conditional_t<kAlterOptional, std::optional<Type>, Type>>;
	using VariantAlterType = std::conditional_t<kAlterPtr, std::unique_ptr<Type>, Type>;

	template <typename X>
	constexpr static bool kCanConstruct =
	    kAlterPtr ? (std::is_base_of_v<Type, X> || std::is_same_v<Type, X>) : std::is_same_v<Type, X>;
	template <typename X>
	constexpr static bool kCanGet =
	    kAlterPtr ? (std::is_base_of_v<Type, X> || std::is_base_of_v<X, Type> || std::is_same_v<Type, X>)
	              : (std::is_base_of_v<X, Type> || std::is_same_v<Type, X>);

	constexpr static bool kCanCheckInitialized = kAlterPtr || kAlterOptional;

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
		static_assert(kCanCheckInitialized);
		val.reset();
	}
	inline static bool IsInitialized(const AlterType &val) {
		static_assert(kCanCheckInitialized);
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
template <class T> constexpr bool kIsUniquePtr = is_unique_ptr_impl<std::decay_t<T>>::value;

template <typename VariantType, typename TypeToCons, bool UniquePtr, size_t I = 0>
constexpr size_t GetVariantConstructIndex() {
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
constexpr bool kVariantCanConstruct = GetVariantConstructIndex<VariantType, TypeToCons, UniquePtr>() != -1;

template <typename VariantType, typename TypeToGet, size_t I = 0> constexpr bool VariantCanGet() {
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
	template <typename X>
	constexpr static bool kCanConstruct = kVariantCanConstruct<Type, X, false> || kVariantCanConstruct<Type, X, true>;
	template <typename X> constexpr static bool kCanGet = VariantCanGet<Type, X>();
	constexpr static bool kCanCheckInitialized = true;

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
template <typename RGType, typename... RGOthers> struct TypeVariant<RGType, RGOthers...> {
	using T =
	    typename VariantCat<typename TypeTraits<RGType>::VariantAlterType, typename TypeVariant<RGOthers...>::T>::T;
};
template <> struct TypeVariant<> {
	using T = std::monostate;
};
} // namespace _details_rg_object_pool_
template <typename... RGTypes> using RGPoolVariant = typename _details_rg_object_pool_::TypeVariant<RGTypes...>::T;
template <typename RGDerived, typename... Types> class RGObjectPool {
private:
	using TypeTuple = typename _details_rg_object_pool_::TypeTuple<Types...>::T;
	template <std::size_t Index> using GetRawType = std::tuple_element_t<Index, std::tuple<Types...>>;
	template <std::size_t Index> using GetAlterType = std::tuple_element_t<Index, TypeTuple>;
	template <std::size_t Index, typename T>
	static constexpr bool kCanConstruct =
	    _details_rg_object_pool_::TypeTraits<GetRawType<Index>>::template kCanConstruct<T>;
	template <std::size_t Index, typename T>
	static constexpr bool kCanGet = _details_rg_object_pool_::TypeTraits<GetRawType<Index>>::template kCanGet<T>;
	template <std::size_t Index>
	static constexpr bool kCanCheckInitialized =
	    _details_rg_object_pool_::TypeTraits<GetRawType<Index>>::kCanCheckInitialized;

	RGKeyMap<TypeTuple> m_pool;

	template <std::size_t Index, typename TypeToCons, typename... Args, typename MapIterator,
	          typename = std::enable_if_t<kCanConstruct<Index, TypeToCons>>>
	inline TypeToCons *initialize(const MapIterator &it, Args &&...args) {
		GetAlterType<Index> &ref = std::get<Index>(it->second);

		using RawType = GetRawType<Index>;
		TypeToCons *ptr = _details_rg_object_pool_::TypeTraits<RawType>::template Initialize<TypeToCons>(
		    ref, std::forward<Args>(args)...);
		// Initialize RGObjectBase
		if constexpr (std::is_base_of_v<RGObjectBase, TypeToCons>) {
			static_assert(std::is_base_of_v<RenderGraphBase, RGDerived> || std::is_base_of_v<RGObjectBase, RGDerived>);

			auto base_ptr = static_cast<RGObjectBase *>(ptr);
			base_ptr->set_key_ptr(&(it->first));
			if constexpr (std::is_base_of_v<RenderGraphBase, RGDerived>)
				base_ptr->set_render_graph_ptr((RenderGraphBase *)static_cast<const RGDerived *>(this));
			else
				base_ptr->set_render_graph_ptr(
				    ((RGObjectBase *)static_cast<const RGDerived *>(this))->GetRenderGraphPtr());
		}
		// Initialize RGResourceBase
		if constexpr (std::is_base_of_v<RGResourceBase, TypeToCons>) {
			auto resource_ptr = static_cast<RGResourceBase *>(ptr);
			if constexpr (std::is_base_of_v<RGPassBase, RGDerived>)
				resource_ptr->set_producer_pass_ptr((RGPassBase *)static_cast<const RGDerived *>(this));
		}
		return ptr;
	}

	template <std::size_t Index, typename MapIterator, typename = std::enable_if_t<kCanCheckInitialized<Index>>>
	inline bool is_initialized(const MapIterator &it) const {
		const GetAlterType<Index> &ref = std::get<Index>(it->second);
		using RawType = GetRawType<Index>;
		return _details_rg_object_pool_::TypeTraits<RawType>::IsInitialized(ref);
	}

	template <std::size_t Index, typename MapIterator, typename = std::enable_if_t<kCanCheckInitialized<Index>>>
	inline void reset(MapIterator &it) {
		GetAlterType<Index> &ref = std::get<Index>(it->second);
		using RawType = GetRawType<Index>;
		_details_rg_object_pool_::TypeTraits<RawType>::Reset(ref);
	}

	template <std::size_t Index, typename TypeToGet, typename MapIterator,
	          typename = std::enable_if_t<kCanGet<Index, TypeToGet>>>
	inline TypeToGet *get(const MapIterator &it) const {
		const GetAlterType<Index> &ref = std::get<Index>(it->second);
		using RawType = GetRawType<Index>;
		return _details_rg_object_pool_::TypeTraits<RawType>::template Get<TypeToGet>(ref);
	}

public:
	inline RGObjectPool() {
		static_assert(std::is_default_constructible_v<TypeTuple> && std::is_move_constructible_v<TypeTuple>);
		// TODO: Debug
		printf("%s\n", typeid(TypeTuple).name());
	}
	inline RGObjectPool(RGObjectPool &&) noexcept = default;
	inline virtual ~RGObjectPool() = default;

protected:
	// Create Tag and Initialize the Main Object
	template <std::size_t Index, typename TypeToCons, typename... Args,
	          typename = std::enable_if_t<kCanConstruct<Index, TypeToCons>>>
	inline TypeToCons *CreateAndInitialize(const RGObjectPoolKey &key, Args &&...args) {
		if (m_pool.find(key) != m_pool.end())
			return nullptr;
		auto it = m_pool.insert({key, TypeTuple{}}).first;
		return initialize<Index, TypeToCons, Args...>(it, std::forward<Args>(args)...);
	}
	// Create Tag Only
	inline void Create(const RGObjectPoolKey &key) {
		if (m_pool.find(key) != m_pool.end())
			return;
		m_pool.insert(std::make_pair(key, TypeTuple{}));
	}
	// Initialize Object of a Tag
	template <std::size_t Index, typename TypeToCons, typename... Args,
	          typename = std::enable_if_t<kCanConstruct<Index, TypeToCons>>>
	inline TypeToCons *Initialize(const RGObjectPoolKey &key, Args &&...args) {
		auto it = m_pool.find(key);
		if (it == m_pool.end())
			return nullptr;
		return initialize<Index, TypeToCons, Args...>(it, std::forward<Args>(args)...);
	}
	// Check whether an Object of a Tag is Initialized
	template <std::size_t Index, typename = std::enable_if_t<kCanCheckInitialized<Index>>>
	inline bool IsInitialized(const RGObjectPoolKey &key) const {
		auto it = m_pool.find(key);
		if (it == m_pool.end())
			return false;
		return is_initialized<Index>(it);
	}
	// Reset an Object of a Tag
	template <std::size_t Index, typename = std::enable_if_t<kCanCheckInitialized<Index>>>
	inline void Reset(const RGObjectPoolKey &key) {
		auto it = m_pool.find(key);
		if (it != m_pool.end())
			reset<Index>(it);
	}
	// Get an Object from a Tag, if not Initialized, Initialize it.
	template <std::size_t Index, typename TypeToCons, typename... Args,
	          typename = std::enable_if_t<kCanGet<Index, TypeToCons> && kCanConstruct<Index, TypeToCons> &&
	                                      kCanCheckInitialized<Index>>>
	inline TypeToCons *InitializeOrGet(const RGObjectPoolKey &key, Args &&...args) {
		auto it = m_pool.find(key);
		if (it == m_pool.end())
			return nullptr;
		return is_initialized<Index>(it) ? (TypeToCons *)get<Index, TypeToCons>(it)
		                                 : initialize<Index, TypeToCons, Args...>(it, std::forward<Args>(args)...);
	}
	// Delete a Tag and its Objects
	inline void Delete(const RGObjectPoolKey &key) { m_pool.erase(key); }
	// Get an Object from a Tag
	template <std::size_t Index, typename Type, typename = std::enable_if_t<kCanGet<Index, Type>>>
	inline Type *Get(const RGObjectPoolKey &key) const {
		auto it = m_pool.find(key);
		if (it == m_pool.end())
			return nullptr;
		return (Type *)get<Index, Type>(it);
	}
};

#pragma endregion

////////////////////////
// SECTION: Resources //
////////////////////////
#pragma region SECTION : Resources

class RGBufferBase : virtual public RGResourceBase {
public:
	inline ~RGBufferBase() override = default;
	inline RGBufferBase() = default;
	inline RGBufferBase(RGBufferBase &&) noexcept = default;

	inline RGResourceType GetType() const final { return RGResourceType::kBuffer; }
	virtual const Ptr<BufferBase> &GetBuffer(uint32_t frame = 0) const = 0;
};

enum class RGAttachmentLoadOp { kClear, kLoad, kDontCare };
class RGImageBase : virtual public RGResourceBase {
public:
	inline ~RGImageBase() override = default;
	inline RGImageBase() = default;
	inline RGImageBase(RGImageBase &&) noexcept = default;

	inline RGResourceType GetType() const final { return RGResourceType::kImage; }
	virtual const Ptr<ImageView> &GetImageView(uint32_t frame = 0) const = 0;

	virtual RGAttachmentLoadOp GetLoadOp() const = 0;
	virtual const VkClearValue &GetClearValue() const = 0;
};
// Alias
class RGResourceAliasBase : virtual public RGResourceBase {
private:
	RGResourceBase *m_resource;

public:
	inline explicit RGResourceAliasBase(RGResourceBase *resource)
	    : m_resource{resource->IsAlias() ? dynamic_cast<RGResourceAliasBase *>(resource)->GetResource() : resource} {}
	inline RGResourceAliasBase(RGResourceAliasBase &&) noexcept = default;

	bool IsAlias() const final { return true; }
	RGResourceState GetState() const final { return m_resource->GetState(); }

	template <typename Type = RGResourceBase, typename = std::enable_if_t<std::is_base_of_v<RGResourceBase, Type>>>
	Type *GetResource() const {
		return dynamic_cast<Type *>(m_resource);
	}
	~RGResourceAliasBase() override = default;
};
class RGImageAlias final : public RGResourceAliasBase, public RGImageBase {
public:
	inline explicit RGImageAlias(RGImageBase *image) : RGResourceAliasBase(image) {}
	inline RGImageAlias(RGImageAlias &&) noexcept = default;
	inline ~RGImageAlias() final = default;

	inline const Ptr<ImageView> &GetImageView(uint32_t frame = 0) const final {
		return GetResource<RGImageBase>()->GetImageView(frame);
	}
	inline RGAttachmentLoadOp GetLoadOp() const final { return GetResource<RGImageBase>()->GetLoadOp(); }
	virtual const VkClearValue &GetClearValue() const final { return GetResource<RGImageBase>()->GetClearValue(); }
};
class RGBufferAlias final : public RGResourceAliasBase, public RGBufferBase {
public:
	inline explicit RGBufferAlias(RGBufferBase *image) : RGResourceAliasBase(image) {}
	inline RGBufferAlias(RGBufferAlias &&) = default;
	inline ~RGBufferAlias() final = default;

	inline const Ptr<BufferBase> &GetBuffer(uint32_t frame = 0) const final {
		return GetResource<RGBufferBase>()->GetBuffer(frame);
	}
};

// Managed Resources
// TODO: Complete this
class RGManagedBuffer final : virtual public RGBufferBase {
public:
	inline RGManagedBuffer() = default;
	inline RGManagedBuffer(RGManagedBuffer &&) noexcept = default;
	~RGManagedBuffer() override = default;

	const Ptr<BufferBase> &GetBuffer(uint32_t frame = 0) const final {
		static Ptr<BufferBase> x;
		return x;
	}
	bool IsAlias() const final { return false; }
	RGResourceState GetState() const final { return RGResourceState::kManaged; }
};
class RGManagedImage final : virtual public RGImageBase {
public:
	inline RGManagedImage() = default;
	inline RGManagedImage(RGManagedImage &&) noexcept = default;
	~RGManagedImage() override = default;

	const Ptr<ImageView> &GetImageView(uint32_t frame = 0) const final {
		static Ptr<ImageView> x;
		return x;
	}
	bool IsAlias() const final { return false; }
	RGResourceState GetState() const final { return RGResourceState::kManaged; }

	inline RGAttachmentLoadOp GetLoadOp() const final { return {}; }
	virtual const VkClearValue &GetClearValue() const final {
		static VkClearValue x;
		return x;
	}
};

// Resource Pool
template <typename RGDerived>
class RGResourcePool : public RGObjectPool<RGDerived, RGPoolVariant<RGBufferBase, RGManagedBuffer, RGBufferAlias,
                                                                    RGImageBase, RGManagedImage, RGImageAlias>> {
private:
	using ResourcePool = RGObjectPool<RGDerived, RGPoolVariant<RGBufferBase, RGManagedBuffer, RGBufferAlias,
	                                                           RGImageBase, RGManagedImage, RGImageAlias>>;

public:
	inline RGResourcePool() = default;
	inline RGResourcePool(RGResourcePool &&) noexcept = default;
	inline ~RGResourcePool() override = default;

protected:
	template <
	    typename Type, typename... Args,
	    typename = std::enable_if_t<std::is_base_of_v<RGBufferBase, Type> || std::is_base_of_v<RGImageBase, Type>>>
	inline Type *CreateResource(const RGObjectPoolKey &resource_key, Args &&...args) {
		return ResourcePool::template CreateAndInitialize<0, Type, Args...>(resource_key, std::forward<Args>(args)...);
	}
	inline void DeleteResource(const RGObjectPoolKey &resource_key) { return ResourcePool::Delete(resource_key); }

	inline RGBufferBase *GetBufferResource(const RGObjectPoolKey &resource_buffer_key) const {
		return ResourcePool::template Get<0, RGBufferBase>(resource_buffer_key);
	}
	inline RGImageBase *GetImageResource(const RGObjectPoolKey &resource_image_key) const {
		return ResourcePool::template Get<0, RGImageBase>(resource_image_key);
	}
};

#pragma endregion

////////////////////////////
// SECTION Resource Input //
////////////////////////////
#pragma region SECTION : Resource Input

enum class RGInputUsage {
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

#define CASE(NAME) case RGInputUsage::NAME:
inline constexpr bool RGInputUsageIsDescriptor(RGInputUsage usage) {
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
inline constexpr VkDescriptorType RGInputUsageGetDescriptorType(RGInputUsage usage) {
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
inline constexpr bool RGInputUsageIsReadOnly(RGInputUsage usage) {
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
inline constexpr bool RGInputUsageForBuffer(RGInputUsage usage) {
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
inline constexpr bool RGInputUsageForImage(RGInputUsage usage) {
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
inline constexpr bool RGInputUsageGetImageLayout(RGInputUsage usage) {
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
template <RGResourceType Type> inline constexpr bool RGInputUsageForType(RGInputUsage usage) {
	if constexpr (Type == RGResourceType::kImage) {
		return RGInputUsageForImage(usage);
	} else {
		return RGInputUsageForBuffer(usage);
	}
};
inline constexpr bool RGInputUsageAll(RGInputUsage) { return true; }

// Input Usage Operation
using RGInputUsageClass = bool(RGInputUsage);
namespace _details_rg_input_usage_op_ {
template <RGInputUsageClass... Args> struct Union;
template <RGInputUsageClass X, RGInputUsageClass... Args> struct Union<X, Args...> {
	constexpr bool operator()(RGInputUsage x) const { return X(x) || Union<Args...>(x); }
};
template <> struct Union<> {
	constexpr bool operator()(RGInputUsage) const { return false; }
};
template <bool... Args(RGInputUsage)> struct Intersect;
template <RGInputUsageClass X, RGInputUsageClass... Args> struct Intersect<X, Args...> {
	constexpr bool operator()(RGInputUsage x) const { return X(x) && Intersect<Args...>(x); }
};
template <> struct Intersect<> {
	constexpr bool operator()(RGInputUsage) const { return true; }
};
} // namespace _details_rg_input_usage_op_
// Union: A | B
template <RGInputUsageClass... Args> inline constexpr bool RGInputUsageUnion(RGInputUsage x) {
	return _details_rg_input_usage_op_::Union<Args...>(x);
}
// Intersect: A & B
template <RGInputUsageClass... Args> inline constexpr bool RGInputUsageIntersect(RGInputUsage x) {
	return _details_rg_input_usage_op_::Intersect<Args...>(x);
}
// Complement: ~X
template <RGInputUsageClass X> inline constexpr bool RGInputUsageComplement(RGInputUsage x) { return !X(x); }
// Minus: A & (~B)
template <RGInputUsageClass A, RGInputUsageClass B> inline constexpr bool RGInputUsageMinus(RGInputUsage x) {
	return RGInputUsageIntersect<A, RGInputUsageComplement<B>>(x);
}

// Resource Input
// TODO: where to use RGInputDescriptorInfo?
struct RGInputDescriptorInfo {
	VkShaderStageFlags stage_flags;
	Ptr<Sampler> sampler;
};
class RGInput {
private:
	RGResourceBase *m_resource_ptr{};
	RGInputUsage m_usage{};

public:
	inline RGInput() = default;
	RGInput(RGResourceBase *resource_ptr, RGInputUsage usage) : m_resource_ptr{resource_ptr}, m_usage{usage} {}

	inline RGResourceBase *GetResource() const { return m_resource_ptr; }
	inline RGInputUsage GetUsage() const { return m_usage; }
};

// Input Pool
template <typename RGDerived>
class RGInputPool : public RGObjectPool<RGDerived, RGInput, RGPoolVariant<RGBufferAlias, RGImageAlias>> {
private:
	using InputPool = RGObjectPool<RGDerived, RGInput, RGPoolVariant<RGBufferAlias, RGImageAlias>>;

	template <typename RGType> inline RGType *create_output(const RGObjectPoolKey &input_key) {
		// RGType can only be RGBufferBase or RGImageBase
		constexpr RGResourceType kResType =
		    std::is_base_of_v<RGImageBase, RGType> ? RGResourceType::kImage : RGResourceType::kBuffer;

		static_assert(std::is_base_of_v<RGPassBase, RGDerived>);
		const RGInput *input = InputPool::template Get<0, RGInput>(input_key);
		if (!input)
			return nullptr;

		if (input->GetResource()->GetProducerPassPtr() == (RGPassBase *)static_cast<RGDerived *>(this))
			return kResType == input->GetResource()->GetType() ? dynamic_cast<RGType *>(input->GetResource()) : nullptr;
		else {
			if constexpr (kResType == RGResourceType::kBuffer) {
				return (input->GetResource()->GetType() == kResType)
				           ? InputPool::template InitializeOrGet<1, RGBufferAlias>(
				                 input_key, dynamic_cast<RGBufferBase *>(input->GetResource()))
				           : nullptr;
			} else {
				return (input->GetResource()->GetType() == kResType)
				           ? InputPool::template InitializeOrGet<1, RGImageAlias>(
				                 input_key, dynamic_cast<RGImageBase *>(input->GetResource()))
				           : nullptr;
			}
		}
	}

public:
	inline RGInputPool() = default;
	inline RGInputPool(RGInputPool &&) noexcept = default;
	inline ~RGInputPool() override = default;

protected:
	template <RGInputUsage Usage, typename = std::enable_if_t<RGInputUsageForBuffer(Usage)>>
	inline void AddInput(const RGObjectPoolKey &input_key, RGBufferBase *buffer) {
		InputPool::template CreateAndInitialize<0, RGInput>(input_key, buffer, Usage);
	}
	template <RGInputUsage Usage, typename = std::enable_if_t<RGInputUsageForImage(Usage)>>
	inline void AddInput(const RGObjectPoolKey &input_key, RGImageBase *image) {
		InputPool::template CreateAndInitialize<0, RGInput>(input_key, image, Usage);
	}
	/* inline const RGInput *GetInput(const RGKey &input_key) const {
	    return InputPool::template Get<0, RGInput>(input_key);
	} */
	inline void RemoveInput(const RGObjectPoolKey &input_key) { InputPool::Delete(input_key); }

	inline RGBufferBase *CreateBufferOutput(const RGObjectPoolKey &input_buffer_key) {
		return create_output<RGBufferBase>(input_buffer_key);
	}
	inline RGImageBase *CreateImageOutput(const RGObjectPoolKey &input_image_key) {
		return create_output<RGImageBase>(input_image_key);
	}
};

#pragma endregion

/* template <typename RGDerived>
class RGInputSlot {
    // Add...
}; */

// Descriptor Pool

// Render Pass
class RGPassBase : virtual public RGObjectBase {};

// TODO: Debug Type Traits
static_assert(std::is_same_v<RGPoolVariant<RGBufferAlias, RGObjectBase, RGResourceBase, RGImageAlias>,
                             std::variant<std::monostate, RGImageAlias, std::unique_ptr<RGResourceBase>,
                                          std::unique_ptr<RGObjectBase>, RGBufferAlias>>);
} // namespace myvk::render_graph

#endif
