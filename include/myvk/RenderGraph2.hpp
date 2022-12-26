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

namespace myvk::render_graph {
///////////////////////////
// SECTION: Base Objects //
///////////////////////////
#pragma region SECTION : Base Objects

class RenderGraphBase;
class RGPassBase;
// Object Base
class RGPoolKey;
class RGObjectBase {
private:
	RenderGraphBase *m_render_graph_ptr{};
	const RGPoolKey *m_key_ptr{};

	inline void set_render_graph_ptr(RenderGraphBase *render_graph_ptr) { m_render_graph_ptr = render_graph_ptr; }
	inline void set_key_ptr(const RGPoolKey *key_ptr) { m_key_ptr = key_ptr; }

	template <typename, typename...> friend class RGPool;
	template <typename> friend class RGDescriptorInputSlot;

public:
	inline RGObjectBase() = default;
	inline virtual ~RGObjectBase() = default;

	inline RenderGraphBase *GetRenderGraphPtr() const { return m_render_graph_ptr; }
	inline const RGPoolKey &GetKey() const { return *m_key_ptr; }

	// Disable Copy
	inline RGObjectBase(RGObjectBase &&r) noexcept = default;
	// inline RGObjectBase &operator=(RGObjectBase &&r) noexcept = default;
};

// Resource Base
enum class RGResourceType { kImage, kBuffer };
enum class RGResourceState { kManaged, kCombinedImage, kExternal };
class RGResourceBase : virtual public RGObjectBase {
private:
	RGPassBase *m_producer_pass_ptr{};

	inline void set_producer_pass_ptr(RGPassBase *producer_pass_ptr) { m_producer_pass_ptr = producer_pass_ptr; }

	template <typename, typename...> friend class RGPool;
	template <typename> friend class RGAliasOutputPool;

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

//////////////////
// SECTION Pool //
//////////////////
#pragma region SECTION : Pool

// Pool Key
class RGPoolKey {
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
	inline RGPoolKey() : _32_{} {}
	template <typename IntType = IDType, typename = std::enable_if_t<std::is_integral_v<IntType>>>
	inline RGPoolKey(std::string_view str, IntType id) : m_str{}, m_len(std::min(str.length(), kMaxStrLen)), m_id(id) {
		std::copy(str.begin(), str.begin() + m_len, m_str);
	}
	inline RGPoolKey(std::string_view str)
	    : m_str{}, m_len(std::min(str.length(), kMaxStrLen)), m_id{std::numeric_limits<IDType>::max()} {
		std::copy(str.begin(), str.begin() + m_len, m_str);
	}
	inline RGPoolKey(const RGPoolKey &r) : _32_{r._32_} {}
	inline RGPoolKey &operator=(const RGPoolKey &r) {
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

	inline bool operator<(const RGPoolKey &r) const { return _32_ < r._32_; }
	inline bool operator>(const RGPoolKey &r) const { return _32_ > r._32_; }
	inline bool operator==(const RGPoolKey &r) const { return _32_ == r._32_; }
	inline bool operator!=(const RGPoolKey &r) const { return _32_ != r._32_; }
	struct Hash {
		inline std::size_t operator()(RGPoolKey const &r) const noexcept {
			return std::get<0>(r._32_) ^ std::get<1>(r._32_) ^ std::get<2>(r._32_) ^ std::get<3>(r._32_);
			// return ((std::get<0>(r._32_) * 37 + std::get<1>(r._32_)) * 37 + std::get<2>(r._32_)) * 37 +
			//        std::get<3>(r._32_);
		}
	};
};
static_assert(sizeof(RGPoolKey) == 32 && std::is_move_constructible_v<RGPoolKey>);

// Pool
namespace _details_rg_pool_ {
template <typename Value> using PoolKeyMap = std::unordered_map<RGPoolKey, Value, RGPoolKey::Hash>;

template <typename Type> class TypeTraits {
private:
	inline constexpr static bool kIsRGObject = std::is_base_of_v<RGObjectBase, Type>;
	inline constexpr static bool kAlterPtr =
	    (kIsRGObject && !std::is_final_v<Type>) || !std::is_move_constructible_v<Type>;
	inline constexpr static bool kAlterOptional = !kAlterPtr && (kIsRGObject || !std::is_default_constructible_v<Type>);

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
template <typename RGType, typename... RGOthers> struct TypeVariant<RGType, RGOthers...> {
	using T =
	    typename VariantCat<typename TypeTraits<RGType>::VariantAlterType, typename TypeVariant<RGOthers...>::T>::T;
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
template <typename... RGTypes> using RGPoolVariant = typename _details_rg_pool_::TypeVariant<RGTypes...>::T;

template <typename Derived, typename... Types> class RGPool {
private:
	using PoolData = _details_rg_pool_::PoolData<Types...>;
	_details_rg_pool_::PoolData<Types...> m_data;

	template <std::size_t Index, typename TypeToCons, typename... Args, typename MapIterator>
	inline TypeToCons *initialize_and_set_rg_data(const MapIterator &it, Args &&...args) {
		TypeToCons *ptr = m_data.template ValueInitialize<Index, TypeToCons>(it, std::forward<Args>(args)...);
		// Initialize RGObjectBase
		if constexpr (std::is_base_of_v<RGObjectBase, TypeToCons>) {
			static_assert(std::is_base_of_v<RenderGraphBase, Derived> || std::is_base_of_v<RGObjectBase, Derived>);

			auto base_ptr = static_cast<RGObjectBase *>(ptr);
			base_ptr->set_key_ptr(&(it->first));
			if constexpr (std::is_base_of_v<RenderGraphBase, Derived>)
				base_ptr->set_render_graph_ptr((RenderGraphBase *)static_cast<const Derived *>(this));
			else
				base_ptr->set_render_graph_ptr(
				    ((RGObjectBase *)static_cast<const Derived *>(this))->GetRenderGraphPtr());
		}
		// Initialize RGResourceBase
		if constexpr (std::is_base_of_v<RGResourceBase, TypeToCons>) {
			auto resource_ptr = static_cast<RGResourceBase *>(ptr);
			if constexpr (std::is_base_of_v<RGPassBase, Derived>)
				resource_ptr->set_producer_pass_ptr((RGPassBase *)static_cast<const Derived *>(this));
		}
		return ptr;
	}

public:
	inline RGPool() = default;
	inline RGPool(RGPool &&) noexcept = default;
	inline virtual ~RGPool() = default;

protected:
	// Get PoolData Pointer
	// inline PoolData &GetPoolData() { return m_data; }
	inline const PoolData &GetPoolData() const { return m_data; }
	// Create Tag and Initialize the Main Object
	template <std::size_t Index, typename TypeToCons, typename... Args>
	inline TypeToCons *CreateAndInitialize(const RGPoolKey &key, Args &&...args) {
		// TODO: Delete this branch ?
		if (m_data.pool.find(key) != m_data.pool.end())
			return nullptr;
		auto it = m_data.pool.insert({key, typename PoolData::TypeTuple{}}).first;
		return initialize_and_set_rg_data<Index, TypeToCons, Args...>(it, std::forward<Args>(args)...);
	}
	// Create Tag Only
	inline void Create(const RGPoolKey &key) {
		if (m_data.pool.find(key) != m_data.pool.end())
			return;
		m_data.pool.insert(std::make_pair(key, typename PoolData::TypeTuple{}));
	}
	// Initialize Object of a Tag
	template <std::size_t Index, typename TypeToCons, typename... Args>
	inline TypeToCons *Initialize(const RGPoolKey &key, Args &&...args) {
		auto it = m_data.pool.find(key);
		return it == m_data.pool.end()
		           ? nullptr
		           : initialize_and_set_rg_data<Index, TypeToCons, Args...>(it, std::forward<Args>(args)...);
	}
	// Check whether an Object of a Tag is Initialized
	template <std::size_t Index> inline bool IsInitialized(const RGPoolKey &key) const {
		auto it = m_data.pool.find(key);
		return it != m_data.pool.end() && m_data.template ValueIsInitialized<Index>(it);
	}
	// Reset an Object of a Tag
	template <std::size_t Index> inline void Reset(const RGPoolKey &key) {
		auto it = m_data.pool.find(key);
		if (it != m_data.pool.end())
			m_data.template ValueReset<Index>(it);
	}
	// Get an Object from a Tag, if not Initialized, Initialize it.
	template <std::size_t Index, typename TypeToCons, typename... Args>
	inline TypeToCons *InitializeOrGet(const RGPoolKey &key, Args &&...args) {
		auto it = m_data.pool.find(key);
		if (it == m_data.pool.end())
			return nullptr;
		return m_data.template ValueIsInitialized<Index>(it)
		           ? (TypeToCons *)m_data.template ValueGet<Index, TypeToCons>(it)
		           : initialize_and_set_rg_data<Index, TypeToCons, Args...>(it, std::forward<Args>(args)...);
	}
	// Delete a Tag and its Objects
	inline void Delete(const RGPoolKey &key) { m_data.pool.erase(key); }
	// Get an Object from a Tag
	template <std::size_t Index, typename Type> inline Type *Get(const RGPoolKey &key) const {
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

class RGBufferBase : virtual public RGResourceBase {
public:
	inline ~RGBufferBase() override = default;
	inline RGBufferBase() = default;
	inline RGBufferBase(RGBufferBase &&) noexcept = default;

	inline RGResourceType GetType() const final { return RGResourceType::kBuffer; }
	virtual const Ptr<BufferBase> &GetVkBuffer() const = 0;
};

enum class RGAttachmentLoadOp { kClear, kLoad, kDontCare };
class RGImageBase : virtual public RGResourceBase {
public:
	inline ~RGImageBase() override = default;
	inline RGImageBase() = default;
	inline RGImageBase(RGImageBase &&) noexcept = default;

	inline RGResourceType GetType() const final { return RGResourceType::kImage; }
	virtual const Ptr<ImageView> &GetVkImageView() const = 0;

	virtual RGAttachmentLoadOp GetLoadOp() const = 0;
	virtual const VkClearValue &GetClearValue() const = 0;
};
// External
// TODO: External barriers (pipeline stage + access + image layout, begin & end)
class RGExternalImageBase : virtual public RGImageBase {
public:
	inline RGExternalImageBase() = default;
	inline RGExternalImageBase(RGExternalImageBase &&) noexcept = default;
	inline ~RGExternalImageBase() override = default;

	inline RGResourceState GetState() const final { return RGResourceState::kExternal; }
	bool IsAlias() const final { return false; }

private:
	RGAttachmentLoadOp m_load_op{RGAttachmentLoadOp::kDontCare};
	VkClearValue m_clear_value{};

public:
	template <RGAttachmentLoadOp LoadOp, typename = std::enable_if_t<LoadOp != RGAttachmentLoadOp::kClear>>
	inline void SetLoadOp() {
		m_load_op = LoadOp;
	}
	template <RGAttachmentLoadOp LoadOp, typename = std::enable_if_t<LoadOp == RGAttachmentLoadOp::kClear>>
	inline void SetLoadOp(const VkClearValue &clear_value) {
		m_load_op = LoadOp;
		m_clear_value = clear_value;
	}
	inline RGAttachmentLoadOp GetLoadOp() const final { return m_load_op; }
	inline const VkClearValue &GetClearValue() const final { return m_clear_value; }
};
class RGExternalBufferBase : virtual public RGBufferBase {
public:
	inline RGExternalBufferBase() = default;
	inline RGExternalBufferBase(RGExternalBufferBase &&) noexcept = default;
	inline ~RGExternalBufferBase() override = default;

	inline RGResourceState GetState() const final { return RGResourceState::kExternal; }
	bool IsAlias() const final { return false; }
};
#ifdef MYVK_ENABLE_GLFW
class RGSwapchainImage final : public RGExternalImageBase {
private:
	Ptr<FrameManager> m_frame_manager;

public:
	inline explicit RGSwapchainImage(Ptr<FrameManager> frame_manager) : m_frame_manager{std::move(frame_manager)} {}
	inline RGSwapchainImage(RGSwapchainImage &&) noexcept = default;
	~RGSwapchainImage() final = default;
	inline const Ptr<ImageView> &GetVkImageView() const final {
		return m_frame_manager->GetCurrentSwapchainImageView();
	}
};
#endif
// Alias
class RGImageAlias final : public RGImageBase {
private:
	RGImageBase *m_pointed_image;

public:
	inline explicit RGImageAlias(RGImageBase *image)
	    : m_pointed_image{image->IsAlias() ? static_cast<RGImageAlias *>(image)->GetPointedResource() : image} {}
	inline RGImageAlias(RGImageAlias &&) noexcept = default;
	inline ~RGImageAlias() final = default;

	inline RGImageBase *GetPointedResource() const { return m_pointed_image; }

	inline const Ptr<ImageView> &GetVkImageView() const final { return m_pointed_image->GetVkImageView(); }
	inline RGAttachmentLoadOp GetLoadOp() const final { return m_pointed_image->GetLoadOp(); }
	const VkClearValue &GetClearValue() const final { return m_pointed_image->GetClearValue(); }

	bool IsAlias() const final { return true; }
	RGResourceState GetState() const final { return m_pointed_image->GetState(); }
};
class RGBufferAlias final : public RGBufferBase {
private:
	RGBufferBase *m_pointed_buffer;

public:
	inline explicit RGBufferAlias(RGBufferBase *buffer)
	    : m_pointed_buffer{buffer->IsAlias() ? static_cast<RGBufferAlias *>(buffer)->GetPointedResource() : buffer} {}
	inline RGBufferAlias(RGBufferAlias &&) = default;
	inline ~RGBufferAlias() final = default;

	inline RGBufferBase *GetPointedResource() const { return m_pointed_buffer; }

	inline const Ptr<BufferBase> &GetVkBuffer() const final { return m_pointed_buffer->GetVkBuffer(); }

	bool IsAlias() const final { return true; }
	RGResourceState GetState() const final { return m_pointed_buffer->GetState(); }
};

// Managed Resources
// TODO: Complete this
class RGManagedBuffer final : public RGBufferBase {
public:
	inline RGManagedBuffer() = default;
	inline RGManagedBuffer(RGManagedBuffer &&) noexcept = default;
	~RGManagedBuffer() override = default;

	const Ptr<BufferBase> &GetVkBuffer() const final {
		static Ptr<BufferBase> x;
		return x;
	}
	bool IsAlias() const final { return false; }
	RGResourceState GetState() const final { return RGResourceState::kManaged; }
};
class RGManagedImage final : public RGImageBase {
public:
	inline RGManagedImage() = default;
	inline RGManagedImage(RGManagedImage &&) noexcept = default;
	~RGManagedImage() override = default;

	const Ptr<ImageView> &GetVkImageView() const final {
		static Ptr<ImageView> x;
		return x;
	}
	bool IsAlias() const final { return false; }
	RGResourceState GetState() const final { return RGResourceState::kManaged; }

	inline RGAttachmentLoadOp GetLoadOp() const final { return {}; }
	const VkClearValue &GetClearValue() const final {
		static VkClearValue x;
		return x;
	}
};

// Resource Pool
namespace _details_rg_pool_ {
using ResourcePoolData =
    PoolData<RGPoolVariant<RGManagedBuffer, RGExternalBufferBase, RGManagedImage, RGExternalImageBase>>;
}
template <typename Derived>
class RGResourcePool
    : public RGPool<Derived,
                    RGPoolVariant<RGManagedBuffer, RGExternalBufferBase, RGManagedImage, RGExternalImageBase>> {
private:
	using ResourcePool =
	    RGPool<Derived, RGPoolVariant<RGManagedBuffer, RGExternalBufferBase, RGManagedImage, RGExternalImageBase>>;

public:
	inline RGResourcePool() = default;
	inline RGResourcePool(RGResourcePool &&) noexcept = default;
	inline ~RGResourcePool() override = default;

protected:
	template <
	    typename Type, typename... Args,
	    typename = std::enable_if_t<std::is_base_of_v<RGBufferBase, Type> || std::is_base_of_v<RGImageBase, Type>>>
	inline Type *CreateResource(const RGPoolKey &resource_key, Args &&...args) {
		return ResourcePool::template CreateAndInitialize<0, Type, Args...>(resource_key, std::forward<Args>(args)...);
	}
	inline void DeleteResource(const RGPoolKey &resource_key) { return ResourcePool::Delete(resource_key); }

	template <typename BufferType = RGBufferBase,
	          typename = std::enable_if_t<std::is_base_of_v<RGBufferBase, BufferType> ||
	                                      std::is_same_v<RGBufferBase, BufferType>>>
	inline BufferType *GetBufferResource(const RGPoolKey &resource_buffer_key) const {
		return ResourcePool::template Get<0, BufferType>(resource_buffer_key);
	}
	template <typename ImageType = RGImageBase, typename = std::enable_if_t<std::is_base_of_v<RGImageBase, ImageType> ||
	                                                                        std::is_same_v<RGImageBase, ImageType>>>
	inline ImageType *GetImageResource(const RGPoolKey &resource_image_key) const {
		return ResourcePool::template Get<0, ImageType>(resource_image_key);
	}
	template <typename ResourceType = RGResourceBase,
	          typename = std::enable_if_t<std::is_base_of_v<RGResourceBase, ResourceType> ||
	                                      std::is_same_v<RGResourceBase, ResourceType>>>
	inline ResourceType *GetResource(const RGPoolKey &resource_image_key) const {
		return ResourcePool::template Get<0, ResourceType>(resource_image_key);
	}
	inline void ClearResources() { ResourcePool::Clear(); }
};

#pragma endregion

////////////////////////////
// SECTION Resource Usage //
////////////////////////////
#pragma region SECTION : Resource Usage

enum class RGUsage {
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
	__RG_USAGE_NUM
};
struct RGUsageInfo {
	VkAccessFlags2 read_access_flags, write_access_flags;

	RGResourceType resource_type;
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

template <RGUsage> inline constexpr RGUsageInfo kRGUsageInfo{};
template <>
inline constexpr RGUsageInfo kRGUsageInfo<RGUsage::kPreserveImage> = {0,     0, RGResourceType::kImage, 0, {}, 0, 0,
                                                                      false, {}};
template <>
inline constexpr RGUsageInfo kRGUsageInfo<RGUsage::kPreserveBuffer> = {0,     0, RGResourceType::kBuffer, 0, {}, 0, 0,
                                                                       false, {}};
template <>
inline constexpr RGUsageInfo kRGUsageInfo<RGUsage::kColorAttachmentW> = {
    0,
    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
    RGResourceType::kImage,
    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    0,
    false,
    {}};
template <>
inline constexpr RGUsageInfo kRGUsageInfo<RGUsage::kColorAttachmentRW> = {
    VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
    RGResourceType::kImage,
    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    0,
    false,
    {}};
template <>
inline constexpr RGUsageInfo kRGUsageInfo<RGUsage::kDepthAttachmentR> = {
    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
    0,
    RGResourceType::kImage,
    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
    0,
    false,
    {}};
template <>
inline constexpr RGUsageInfo kRGUsageInfo<RGUsage::kDepthAttachmentRW> = {
    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    RGResourceType::kImage,
    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
    0,
    false,
    {}};
template <>
inline constexpr RGUsageInfo kRGUsageInfo<RGUsage::kInputAttachment> = {VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT,
                                                                        0,
                                                                        RGResourceType::kImage,
                                                                        VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
                                                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                                                        0,
                                                                        true,
                                                                        VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT};
// TODO: Is it correct?
template <>
inline constexpr RGUsageInfo kRGUsageInfo<RGUsage::kPresent> = {
    0, 0, RGResourceType::kImage, 0, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 0, 0, false, {}};
template <>
inline constexpr RGUsageInfo kRGUsageInfo<RGUsage::kSampledImage> = {
    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,      0, RGResourceType::kImage,           VK_IMAGE_USAGE_SAMPLED_BIT,
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, __PIPELINE_STAGE_ALL_SHADERS_BIT, true,
    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER};
template <>
inline constexpr RGUsageInfo kRGUsageInfo<RGUsage::kStorageImageR> = {VK_ACCESS_2_SHADER_STORAGE_READ_BIT, //
                                                                      0,
                                                                      RGResourceType::kImage,
                                                                      VK_IMAGE_USAGE_STORAGE_BIT,
                                                                      VK_IMAGE_LAYOUT_GENERAL,
                                                                      0,
                                                                      __PIPELINE_STAGE_ALL_SHADERS_BIT,
                                                                      true,
                                                                      VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
template <>
inline constexpr RGUsageInfo kRGUsageInfo<RGUsage::kStorageImageW> = {0,
                                                                      VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                                                                      RGResourceType::kImage,
                                                                      VK_IMAGE_USAGE_STORAGE_BIT,
                                                                      VK_IMAGE_LAYOUT_GENERAL,
                                                                      0,
                                                                      __PIPELINE_STAGE_ALL_SHADERS_BIT,
                                                                      true,
                                                                      VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
template <>
inline constexpr RGUsageInfo kRGUsageInfo<RGUsage::kStorageImageRW> = {VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                                                                       VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                                                                       RGResourceType::kImage,
                                                                       VK_IMAGE_USAGE_STORAGE_BIT,
                                                                       VK_IMAGE_LAYOUT_GENERAL,
                                                                       0,
                                                                       __PIPELINE_STAGE_ALL_SHADERS_BIT,
                                                                       true,
                                                                       VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
template <>
inline constexpr RGUsageInfo kRGUsageInfo<RGUsage::kUniformBuffer> = {VK_ACCESS_2_UNIFORM_READ_BIT, //
                                                                      0,
                                                                      RGResourceType::kBuffer,
                                                                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                                      {},
                                                                      0,
                                                                      __PIPELINE_STAGE_ALL_SHADERS_BIT,
                                                                      true,
                                                                      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER};
template <>
inline constexpr RGUsageInfo kRGUsageInfo<RGUsage::kStorageBufferR> = {VK_ACCESS_2_SHADER_STORAGE_READ_BIT, //
                                                                       0,
                                                                       RGResourceType::kBuffer,
                                                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                                       {},
                                                                       0,
                                                                       __PIPELINE_STAGE_ALL_SHADERS_BIT,
                                                                       true,
                                                                       VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
template <>
inline constexpr RGUsageInfo kRGUsageInfo<RGUsage::kStorageBufferW> = {0, //
                                                                       VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                                                                       RGResourceType::kBuffer,
                                                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                                       {},
                                                                       0,
                                                                       __PIPELINE_STAGE_ALL_SHADERS_BIT,
                                                                       true,
                                                                       VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
template <>
inline constexpr RGUsageInfo kRGUsageInfo<RGUsage::kStorageBufferRW> = {VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                                                                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                                                                        RGResourceType::kBuffer,
                                                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                                        {},
                                                                        0,
                                                                        __PIPELINE_STAGE_ALL_SHADERS_BIT,
                                                                        true,
                                                                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
template <>
inline constexpr RGUsageInfo kRGUsageInfo<RGUsage::kVertexBuffer> = {VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
                                                                     0,
                                                                     RGResourceType::kBuffer,
                                                                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                                                     {},
                                                                     VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT,
                                                                     0,
                                                                     false,
                                                                     {}};
template <>
inline constexpr RGUsageInfo kRGUsageInfo<RGUsage::kIndexBuffer> = {VK_ACCESS_2_INDEX_READ_BIT, //
                                                                    0,
                                                                    RGResourceType::kBuffer,
                                                                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                                                    {},
                                                                    VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT,
                                                                    0,
                                                                    false,
                                                                    {}};
template <>
inline constexpr RGUsageInfo kRGUsageInfo<RGUsage::kTransferImageSrc> = {
    VK_ACCESS_2_TRANSFER_READ_BIT, //
    0,
    RGResourceType::kImage,
    VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    0,
    VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT, // Copy or Blit as SRC
    false,
    {}};
template <>
inline constexpr RGUsageInfo kRGUsageInfo<RGUsage::kTransferImageDst> = {
    0, //
    VK_ACCESS_2_TRANSFER_WRITE_BIT,
    RGResourceType::kImage,
    VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    0,
    VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT |
        VK_PIPELINE_STAGE_2_CLEAR_BIT, // Copy, Blit, Clear as DST
    false,
    {}};
template <>
inline constexpr RGUsageInfo kRGUsageInfo<RGUsage::kTransferBufferSrc> = {
    VK_ACCESS_2_TRANSFER_READ_BIT, //
    0,
    RGResourceType::kBuffer,
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    {},
    VK_PIPELINE_STAGE_2_COPY_BIT, // ONLY Copy as SRC
    0,
    false,
    {}};
template <>
inline constexpr RGUsageInfo kRGUsageInfo<RGUsage::kTransferBufferDst> = {
    0, //
    VK_ACCESS_2_TRANSFER_WRITE_BIT,
    RGResourceType::kBuffer,
    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    {},
    0,
    VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT, // Copy or Fill as DST
    false,
    {}};
template <typename IndexSequence> class RGUsageInfoTable;
template <std::size_t... Indices> class RGUsageInfoTable<std::index_sequence<Indices...>> {
private:
	inline static constexpr RGUsageInfo kArr[] = {(kRGUsageInfo<(RGUsage)Indices>)...};

public:
	inline constexpr const RGUsageInfo &operator[](RGUsage usage) const { return kArr[(std::size_t)usage]; }
};
inline constexpr RGUsageInfoTable<std::make_index_sequence<(std::size_t)RGUsage::__RG_USAGE_NUM>> kRGUsageInfoTable{};

// Is Descriptor
template <RGUsage Usage> inline constexpr bool kRGUsageIsDescriptor = kRGUsageInfo<Usage>.is_descriptor;
inline constexpr bool RGUsageIsDescriptor(RGUsage usage) { return kRGUsageInfoTable[usage].is_descriptor; }
// Get Descriptor Type
template <RGUsage Usage>
inline constexpr VkDescriptorType kRGUsageGetDescriptorType = kRGUsageInfo<Usage>.descriptor_type;
inline constexpr VkDescriptorType RGUsageGetDescriptorType(RGUsage usage) {
	return kRGUsageInfoTable[usage].descriptor_type;
}
// Get Read Access Flags
template <RGUsage Usage>
inline constexpr VkAccessFlags2 kRGUsageGetReadAccessFlags = kRGUsageInfo<Usage>.read_access_flags;
inline constexpr VkAccessFlags2 RGUsageGetReadAccessFlags(RGUsage usage) {
	return kRGUsageInfoTable[usage].read_access_flags;
}
// Get Write Access Flags
template <RGUsage Usage>
inline constexpr VkAccessFlags2 kRGUsageGetWriteAccessFlags = kRGUsageInfo<Usage>.write_access_flags;
inline constexpr VkAccessFlags2 RGUsageGetWriteAccessFlags(RGUsage usage) {
	return kRGUsageInfoTable[usage].write_access_flags;
}
// Get Access Flags
template <RGUsage Usage>
inline constexpr VkAccessFlags2 kRGUsageGetAccessFlags =
    kRGUsageGetReadAccessFlags<Usage> | kRGUsageGetWriteAccessFlags<Usage>;
inline constexpr VkAccessFlags2 RGUsageGetAccessFlags(RGUsage usage) {
	return RGUsageGetReadAccessFlags(usage) | RGUsageGetWriteAccessFlags(usage);
}
// Is Read-Only
template <RGUsage Usage> inline constexpr bool kRGUsageIsReadOnly = kRGUsageGetWriteAccessFlags<Usage> == 0;
inline constexpr bool RGUsageIsReadOnly(RGUsage usage) { return RGUsageGetWriteAccessFlags(usage) == 0; }
// Is For Buffer
template <RGUsage Usage>
inline constexpr bool kRGUsageForBuffer = kRGUsageInfo<Usage>.resource_type == RGResourceType::kBuffer;
inline constexpr bool RGUsageForBuffer(RGUsage usage) {
	return kRGUsageInfoTable[usage].resource_type == RGResourceType::kBuffer;
}
// Is For Image
template <RGUsage Usage>
inline constexpr bool kRGUsageForImage = kRGUsageInfo<Usage>.resource_type == RGResourceType::kImage;
inline constexpr bool RGUsageForImage(RGUsage usage) {
	return kRGUsageInfoTable[usage].resource_type == RGResourceType::kImage;
}
// Is Color Attachment
template <RGUsage Usage>
inline constexpr bool kRGUsageIsColorAttachment =
    Usage == RGUsage::kColorAttachmentW || Usage == RGUsage::kColorAttachmentRW;
inline constexpr bool RGUsageIsColorAttachment(RGUsage usage) {
	return usage == RGUsage::kColorAttachmentW || usage == RGUsage::kColorAttachmentRW;
}
// Is Depth Attachment
template <RGUsage Usage>
inline constexpr bool kRGUsageIsDepthAttachment =
    Usage == RGUsage::kDepthAttachmentR || Usage == RGUsage::kDepthAttachmentRW;
inline constexpr bool RGUsageIsDepthAttachment(RGUsage usage) {
	return usage == RGUsage::kDepthAttachmentR || usage == RGUsage::kDepthAttachmentRW;
}
// Is Attachment
template <RGUsage Usage>
inline constexpr bool kRGUsageIsAttachment =
    kRGUsageIsColorAttachment<Usage> || kRGUsageIsDepthAttachment<Usage> || Usage == RGUsage::kInputAttachment;
inline constexpr bool RGUsageIsAttachment(RGUsage usage) {
	return RGUsageIsColorAttachment(usage) || RGUsageIsDepthAttachment(usage) || usage == RGUsage::kInputAttachment;
}
// Has Specified Pipeline Stages
template <RGUsage Usage>
inline constexpr bool kRGUsageHasSpecifiedPipelineStages = kRGUsageInfo<Usage>.specified_pipeline_stages;
inline constexpr bool RGUsageHasSpecifiedPipelineStages(RGUsage usage) {
	return kRGUsageInfoTable[usage].specified_pipeline_stages;
}
// Get Specified Pipeline Stages
template <RGUsage Usage>
inline constexpr VkPipelineStageFlags2 kRGUsageGetSpecifiedPipelineStages =
    kRGUsageInfo<Usage>.specified_pipeline_stages;
inline constexpr VkPipelineStageFlags2 RGUsageGetSpecifiedPipelineStages(RGUsage usage) {
	return kRGUsageInfoTable[usage].specified_pipeline_stages;
}
// Get Optional Pipeline Stages
template <RGUsage Usage>
inline constexpr VkPipelineStageFlags2 kRGUsageGetOptionalPipelineStages = kRGUsageInfo<Usage>.optional_pipeline_stages;
inline constexpr VkPipelineStageFlags2 RGUsageGetOptionalPipelineStages(RGUsage usage) {
	return kRGUsageInfoTable[usage].optional_pipeline_stages;
}
// Get Image Layout
template <RGUsage Usage> inline constexpr VkImageLayout kRGUsageGetImageLayout = kRGUsageInfo<Usage>.image_layout;
inline constexpr VkImageLayout RGUsageGetImageLayout(RGUsage usage) { return kRGUsageInfoTable[usage].image_layout; }
// Get Resource Creation Usages
template <RGUsage Usage>
inline constexpr VkFlags kRGUsageGetCreationUsages = kRGUsageInfo<Usage>.resource_creation_usages;
inline constexpr VkFlags RGUsageGetCreationUsages(RGUsage usage) {
	return kRGUsageInfoTable[usage].resource_creation_usages;
}
// inline constexpr bool RGUsageAll(RGUsage) { return true; }

// Input Usage Operation
/*using RGUsageClass = bool(RGUsage);
namespace _details_rg_input_usage_op_ {
template <RGUsageClass... Args> struct Union;
template <RGUsageClass X, RGUsageClass... Args> struct Union<X, Args...> {
    constexpr bool operator()(RGUsage x) const { return X(x) || Union<Args...>(x); }
};
template <> struct Union<> {
    constexpr bool operator()(RGUsage) const { return false; }
};
template <bool... Args(RGUsage)> struct Intersect;
template <RGUsageClass X, RGUsageClass... Args> struct Intersect<X, Args...> {
    constexpr bool operator()(RGUsage x) const { return X(x) && Intersect<Args...>(x); }
};
template <> struct Intersect<> {
    constexpr bool operator()(RGUsage) const { return true; }
};
} // namespace _details_rg_input_usage_op_
// Union: A | B
template <RGUsageClass... Args> inline constexpr bool RGUsageUnion(RGUsage x) {
    return _details_rg_input_usage_op_::Union<Args...>(x);
}
// Intersect: A & B
template <RGUsageClass... Args> inline constexpr bool RGUsageIntersect(RGUsage x) {
    return _details_rg_input_usage_op_::Intersect<Args...>(x);
}
// Complement: ~X
template <RGUsageClass X> inline constexpr bool RGUsageComplement(RGUsage x) { return !X(x); }
// Minus: A & (~B)
template <RGUsageClass A, RGUsageClass B> inline constexpr bool RGUsageMinus(RGUsage x) {
    return RGUsageIntersect<A, RGUsageComplement<B>>(x);
}*/

#pragma endregion

//////////////////////////
// SECTION Resource I/O //
//////////////////////////
#pragma region SECTION : Resource IO

// Alias Output Pool (for Sub-pass)
template <typename Derived>
class RGAliasOutputPool : public RGPool<Derived, RGBufferAlias>, public RGPool<Derived, RGImageAlias> {
private:
	template <typename RGType, typename RGAliasType>
	inline RGType *make_alias_output(const RGPoolKey &output_key, RGType *resource) {
		using Pool = RGPool<Derived, RGAliasType>;
		RGAliasType *alias = Pool ::template Get<0, RGAliasType>(output_key);
		if (alias == nullptr) {
			alias = Pool ::template CreateAndInitialize<0, RGAliasType>(output_key, resource);
		} else {
			if (alias->GetPointedResource() == resource)
				return alias;
			alias = Pool ::template Initialize<0, RGAliasType>(output_key, resource);
		}
		// Redirect ProducerPassPtr
		static_cast<RGResourceBase *>(alias)->set_producer_pass_ptr(resource->GetProducerPassPtr());
		return alias;
	}

public:
	inline RGAliasOutputPool() = default;
	inline RGAliasOutputPool(RGAliasOutputPool &&) noexcept = default;
	inline virtual ~RGAliasOutputPool() = default;

protected:
	inline RGBufferBase *MakeBufferAliasOutput(const RGPoolKey &buffer_alias_output_key, RGBufferBase *buffer_output) {
		return make_alias_output<RGBufferBase, RGBufferAlias>(buffer_alias_output_key, buffer_output);
	}
	inline RGImageBase *MakeImageAliasOutput(const RGPoolKey &image_alias_output_key, RGImageBase *image_output) {
		return make_alias_output<RGImageBase, RGImageAlias>(image_alias_output_key, image_output);
	}
	// TODO: MakeCombinedImageOutput
	inline void RemoveBufferAliasOutput(const RGPoolKey &buffer_alias_output_key) {
		RGPool<Derived, RGBufferAlias>::Delete(buffer_alias_output_key);
	}
	inline void RemoveImageAliasOutput(const RGPoolKey &image_alias_output_key) {
		RGPool<Derived, RGImageAlias>::Delete(image_alias_output_key);
	}
	inline void ClearBufferAliasOutputs() { RGPool<Derived, RGBufferAlias>::Clear(); }
	inline void ClearImageAliasOutputs() { RGPool<Derived, RGImageAlias>::Clear(); }
};

// Resource Input
class RGInput {
private:
	std::variant<RGImageBase *, RGBufferBase *> m_resource_ptr{};
	RGUsage m_usage{};
	VkPipelineStageFlags2 m_usage_pipeline_stages{};
	uint32_t m_descriptor_binding{UINT32_MAX};
	uint32_t m_attachment_index{UINT32_MAX};

public:
	inline RGInput() = default;
	template <typename RGType>
	RGInput(RGType *resource_ptr, RGUsage usage, VkPipelineStageFlags2 usage_pipeline_stages,
	        uint32_t descriptor_binding = UINT32_MAX, uint32_t attachment_index = UINT32_MAX)
	    : m_resource_ptr{resource_ptr}, m_usage{usage}, m_usage_pipeline_stages{usage_pipeline_stages},
	      m_descriptor_binding{descriptor_binding}, m_attachment_index{attachment_index} {}
	template <typename RGType = RGResourceBase> inline RGType *GetResource() const {
		return std::visit(
		    [](auto arg) -> RGType * {
			    using CURType = std::decay_t<decltype(*arg)>;
			    if constexpr (std::is_same_v<RGType, CURType> || std::is_base_of_v<RGType, CURType>)
				    return arg;
			    return nullptr;
		    },
		    m_resource_ptr);
	}
	inline RGUsage GetUsage() const { return m_usage; }
	inline VkPipelineStageFlags2 GetUsagePipelineStages() const { return m_usage_pipeline_stages; }
	inline uint32_t GetDescriptorBinding() const { return m_descriptor_binding; }
	inline uint32_t GetAttachmentIndex() const { return m_attachment_index; }
};

// Input Pool
namespace _details_rg_pool_ {
using InputPoolData = PoolData<RGInput, RGPoolVariant<RGBufferAlias, RGImageAlias>>;
}
template <typename Derived>
class RGInputPool : public RGPool<Derived, RGInput, RGPoolVariant<RGBufferAlias, RGImageAlias>> {
private:
	using InputPool = RGPool<Derived, RGInput, RGPoolVariant<RGBufferAlias, RGImageAlias>>;

	template <typename... Args> inline RGInput *add_input(const RGPoolKey &input_key, Args &&...input_args) {
		return InputPool::template CreateAndInitialize<0, RGInput>(input_key, std::forward<Args>(input_args)...);
	}

	template <typename RGType, typename RGAliasType> inline RGType *make_output(const RGPoolKey &input_key) {
		const RGInput *input = InputPool::template Get<0, RGInput>(input_key);
		assert(input && !RGUsageIsReadOnly(input->GetUsage()));
		if (!input || RGUsageIsReadOnly(input->GetUsage())) // Read-Only input should not produce an output
			return nullptr;
		RGType *resource = input->GetResource<RGType>();
		assert(resource);
		if (!resource)
			return nullptr;
		else if (resource->GetProducerPassPtr() == (RGPassBase *)static_cast<Derived *>(this))
			return resource;
		else
			return InputPool::template InitializeOrGet<1, RGAliasType>(input_key, resource);
	}

	template <typename> friend class RGDescriptorInputSlot;
	template <typename> friend class RGAttachmentInputSlot;

public:
	inline RGInputPool() { static_assert(std::is_base_of_v<RGPassBase, Derived>); }
	inline RGInputPool(RGInputPool &&) noexcept = default;
	inline ~RGInputPool() override = default;

protected:
	template <RGUsage Usage,
	          typename = std::enable_if_t<!kRGUsageIsAttachment<Usage> && !kRGUsageIsDescriptor<Usage> &&
	                                      kRGUsageHasSpecifiedPipelineStages<Usage> && kRGUsageForBuffer<Usage>>>
	inline bool AddInput(const RGPoolKey &input_key, RGBufferBase *buffer) {
		return add_input(input_key, buffer, Usage, kRGUsageGetSpecifiedPipelineStages<Usage>);
	}
	template <RGUsage Usage, VkPipelineStageFlags2 PipelineStageFlags,
	          typename = std::enable_if_t<!kRGUsageIsAttachment<Usage> && !kRGUsageIsDescriptor<Usage> &&
	                                      !kRGUsageHasSpecifiedPipelineStages<Usage> &&
	                                      (PipelineStageFlags & kRGUsageGetOptionalPipelineStages<Usage>) ==
	                                          PipelineStageFlags &&
	                                      kRGUsageForBuffer<Usage>>>
	inline bool AddInput(const RGPoolKey &input_key, RGBufferBase *buffer) {
		return add_input(input_key, buffer, Usage, PipelineStageFlags);
	}
	template <RGUsage Usage,
	          typename = std::enable_if_t<!kRGUsageIsAttachment<Usage> && !kRGUsageIsDescriptor<Usage> &&
	                                      kRGUsageHasSpecifiedPipelineStages<Usage> && kRGUsageForImage<Usage>>>
	inline bool AddInput(const RGPoolKey &input_key, RGImageBase *image) {
		return add_input(input_key, image, Usage, kRGUsageGetSpecifiedPipelineStages<Usage>);
	}
	template <RGUsage Usage, VkPipelineStageFlags2 PipelineStageFlags,
	          typename = std::enable_if_t<!kRGUsageIsAttachment<Usage> && !kRGUsageIsDescriptor<Usage> &&
	                                      !kRGUsageHasSpecifiedPipelineStages<Usage> &&
	                                      (PipelineStageFlags & kRGUsageGetOptionalPipelineStages<Usage>) ==
	                                          PipelineStageFlags &&
	                                      kRGUsageForImage<Usage>>>
	inline bool AddInput(const RGPoolKey &input_key, RGImageBase *image) {
		return add_input(input_key, image, Usage, PipelineStageFlags);
	}
	// TODO: AddCombinedImageInput()
	inline const RGInput *GetInput(const RGPoolKey &input_key) const {
		return InputPool::template Get<0, RGInput>(input_key);
	}
	inline RGBufferBase *MakeBufferOutput(const RGPoolKey &input_buffer_key) {
		return make_output<RGBufferBase, RGBufferAlias>(input_buffer_key);
	}
	inline RGImageBase *MakeImageOutput(const RGPoolKey &input_image_key) {
		return make_output<RGImageBase, RGImageAlias>(input_image_key);
	}
	inline void RemoveInput(const RGPoolKey &input_key);
	inline void ClearInputs();
};

class RGDescriptorBinding {
private:
	const RGInput *m_p_input{};
	Ptr<Sampler> m_sampler{};

public:
	inline RGDescriptorBinding() = default;
	inline explicit RGDescriptorBinding(const RGInput *input, const Ptr<Sampler> &sampler = nullptr)
	    : m_p_input{input}, m_sampler{sampler} {}
	inline void SetInput(const RGInput *input) { m_p_input = input; }
	inline void SetSampler(const Ptr<Sampler> &sampler) { m_sampler = sampler; }
	inline void Reset() {
		m_sampler.reset();
		m_p_input = nullptr;
	}
	inline const RGInput *GetInputPtr() const { return m_p_input; }
	inline const Ptr<Sampler> &GetVkSampler() const { return m_sampler; }
};

class RGDescriptorSetData : public RGObjectBase {
private:
	std::unordered_map<uint32_t, RGDescriptorBinding> m_bindings;

	mutable Ptr<DescriptorSetLayout> m_descriptor_set_layout;
	mutable std::vector<Ptr<DescriptorSet>> m_descriptor_sets;
	mutable bool m_updated = true;

public:
	inline bool IsBindingExist(uint32_t binding) const { return m_bindings.find(binding) != m_bindings.end(); }
	inline void AddBinding(uint32_t binding, const RGInput *input, const Ptr<Sampler> &sampler = nullptr) {
		m_bindings.insert({binding, RGDescriptorBinding{input, sampler}});
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

	const Ptr<DescriptorSetLayout> &GetVkDescriptorSetLayout() const;
};

class RGAttachmentData {
private:
	std::vector<const RGInput *> m_color_attachments, m_input_attachments;
	const RGInput *m_depth_attachment;

public:
	inline RGAttachmentData() {
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
	inline void AddColorAttachment(uint32_t index, const RGInput *input) {
		if (m_color_attachments.size() <= index)
			m_color_attachments.resize(index + 1);
		m_color_attachments[index] = input;
	}
	inline void AddInputAttachment(uint32_t index, const RGInput *input) {
		if (m_input_attachments.size() <= index)
			m_input_attachments.resize(index + 1);
		m_input_attachments[index] = input;
	}
	inline void SetDepthAttachment(const RGInput *input) { m_depth_attachment = input; }
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

template <typename Derived> class RGDescriptorInputSlot {
private:
	RGDescriptorSetData m_descriptor_set_data;

	inline RGInputPool<Derived> *get_input_pool_ptr() {
		static_assert(std::is_base_of_v<RGInputPool<Derived>, Derived>);
		return (RGInputPool<Derived> *)static_cast<Derived *>(this);
	}
	inline const RGInputPool<Derived> *get_input_pool_ptr() const {
		static_assert(std::is_base_of_v<RGInputPool<Derived>, Derived>);
		return (const RGInputPool<Derived> *)static_cast<const Derived *>(this);
	}

	template <typename RGType>
	inline RGInput *add_input_descriptor(const RGPoolKey &input_key, RGType *resource, RGUsage usage,
	                                     VkPipelineStageFlags2 pipeline_stage_flags, uint32_t binding,
	                                     const Ptr<Sampler> &sampler = nullptr,
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

	inline void pre_remove_input(const RGInput *input) {
		if (RGUsageIsDescriptor(input->GetUsage())) {
			assert(~(input->GetDescriptorBinding()));
			m_descriptor_set_data.RemoveBinding(input->GetDescriptorBinding());
		}
	}
	inline void pre_clear_inputs() { m_descriptor_set_data.ClearBindings(); }

	template <typename> friend class RGInputPool;
	template <typename> friend class RGAttachmentInputSlot;

public:
	inline RGDescriptorInputSlot() {
		static_assert(std::is_base_of_v<RGObjectBase, Derived> || std::is_base_of_v<RenderGraphBase, Derived>);
		if constexpr (std::is_base_of_v<RGObjectBase, Derived>)
			m_descriptor_set_data.set_render_graph_ptr(
			    ((RGObjectBase *)static_cast<Derived *>(this))->GetRenderGraphPtr());
		else
			m_descriptor_set_data.set_render_graph_ptr((RenderGraphBase *)static_cast<Derived *>(this));
	}
	inline RGDescriptorInputSlot(RGDescriptorInputSlot &&) noexcept = default;
	inline ~RGDescriptorInputSlot() = default;

protected:
	inline const RGDescriptorSetData &GetDescriptorSetData() const { return m_descriptor_set_data; }

	template <uint32_t Binding, RGUsage Usage,
	          typename = std::enable_if_t<!kRGUsageIsAttachment<Usage> && kRGUsageIsDescriptor<Usage> &&
	                                      kRGUsageHasSpecifiedPipelineStages<Usage> && kRGUsageForBuffer<Usage>>>
	inline bool AddDescriptorInput(const RGPoolKey &input_key, RGBufferBase *buffer) {
		return add_input_descriptor(input_key, buffer, Usage, kRGUsageGetSpecifiedPipelineStages<Usage>, Binding);
	}
	template <
	    uint32_t Binding, RGUsage Usage, VkPipelineStageFlags2 PipelineStageFlags,
	    typename = std::enable_if_t<
	        !kRGUsageIsAttachment<Usage> && kRGUsageIsDescriptor<Usage> && !kRGUsageHasSpecifiedPipelineStages<Usage> &&
	        (PipelineStageFlags & kRGUsageGetOptionalPipelineStages<Usage>) == PipelineStageFlags &&
	        kRGUsageForBuffer<Usage>>>
	inline bool AddDescriptorInput(const RGPoolKey &input_key, RGBufferBase *buffer) {
		return add_input_descriptor(input_key, buffer, Usage, PipelineStageFlags, Binding);
	}
	template <uint32_t Binding, RGUsage Usage,
	          typename = std::enable_if_t<!kRGUsageIsAttachment<Usage> && Usage != RGUsage::kSampledImage &&
	                                      kRGUsageIsDescriptor<Usage> && kRGUsageHasSpecifiedPipelineStages<Usage> &&
	                                      kRGUsageForImage<Usage>>>
	inline bool AddDescriptorInput(const RGPoolKey &input_key, RGImageBase *image) {
		return add_input_descriptor(input_key, image, Usage, kRGUsageGetSpecifiedPipelineStages<Usage>, Binding);
	}
	template <uint32_t Binding, RGUsage Usage, VkPipelineStageFlags2 PipelineStageFlags,
	          typename = std::enable_if_t<!kRGUsageIsAttachment<Usage> && Usage != RGUsage::kSampledImage &&
	                                      kRGUsageIsDescriptor<Usage> && !kRGUsageHasSpecifiedPipelineStages<Usage> &&
	                                      (PipelineStageFlags & kRGUsageGetOptionalPipelineStages<Usage>) ==
	                                          PipelineStageFlags &&
	                                      kRGUsageForImage<Usage>>>
	inline bool AddDescriptorInput(const RGPoolKey &input_key, RGImageBase *image) {
		return add_input_descriptor(input_key, image, Usage, PipelineStageFlags, Binding);
	}
	template <uint32_t Binding, RGUsage Usage,
	          typename = std::enable_if_t<Usage == RGUsage::kSampledImage &&
	                                      kRGUsageHasSpecifiedPipelineStages<Usage> && kRGUsageForImage<Usage>>>
	inline bool AddDescriptorInput(const RGPoolKey &input_key, RGImageBase *image, const Ptr<Sampler> &sampler) {
		return add_input_descriptor(input_key, image, Usage, kRGUsageGetSpecifiedPipelineStages<Usage>, Binding,
		                            sampler);
	}
	template <uint32_t Binding, RGUsage Usage, VkPipelineStageFlags2 PipelineStageFlags,
	          typename = std::enable_if_t<
	              Usage == RGUsage::kSampledImage && !kRGUsageHasSpecifiedPipelineStages<Usage> &&
	              (PipelineStageFlags & kRGUsageGetOptionalPipelineStages<Usage>) == PipelineStageFlags &&
	              kRGUsageForImage<Usage>>>
	inline bool AddDescriptorInput(const RGPoolKey &input_key, RGImageBase *image, const Ptr<Sampler> &sampler) {
		return add_input_descriptor(input_key, image, Usage, PipelineStageFlags, Binding, sampler);
	}
	inline const Ptr<DescriptorSetLayout> &GetDescriptorSetLayout() const {
		return m_descriptor_set_data.GetVkDescriptorSetLayout();
	}
};

template <typename Derived> class RGAttachmentInputSlot {
private:
	RGAttachmentData m_attachment_data;

	inline RGInputPool<Derived> *get_input_pool_ptr() {
		static_assert(std::is_base_of_v<RGInputPool<Derived>, Derived>);
		return (RGInputPool<Derived> *)static_cast<Derived *>(this);
	}
	inline const RGInputPool<Derived> *get_input_pool_ptr() const {
		static_assert(std::is_base_of_v<RGInputPool<Derived>, Derived>);
		return (const RGInputPool<Derived> *)static_cast<const Derived *>(this);
	}

	inline RGDescriptorInputSlot<Derived> *get_descriptor_slot_ptr() {
		static_assert(std::is_base_of_v<RGDescriptorInputSlot<Derived>, Derived>);
		return (RGDescriptorInputSlot<Derived> *)static_cast<Derived *>(this);
	}
	inline const RGDescriptorInputSlot<Derived> *get_descriptor_slot_ptr() const {
		static_assert(std::is_base_of_v<RGDescriptorInputSlot<Derived>, Derived>);
		return (const RGDescriptorInputSlot<Derived> *)static_cast<const Derived *>(this);
	}

	inline void pre_remove_input(const RGInput *input) {
		if (RGUsageIsDepthAttachment(input->GetUsage()))
			m_attachment_data.ResetDepthAttachment();
		else if (RGUsageIsColorAttachment(input->GetUsage())) {
			assert(~(input->GetAttachmentIndex()));
			m_attachment_data.RemoveColorAttachment(input->GetAttachmentIndex());
		} else if (input->GetUsage() == RGUsage::kInputAttachment) {
			assert(~(input->GetAttachmentIndex()));
			m_attachment_data.RemoveInputAttachment(input->GetAttachmentIndex());
		}
	}
	inline void pre_clear_inputs() { m_attachment_data.ClearAttachmens(); }

	template <typename> friend class RGInputPool;

public:
	inline RGAttachmentInputSlot() {
		// TODO: Should RGAttachmentData be RGObject ?
		/* static_assert(std::is_base_of_v<RGObjectBase, Derived> || std::is_base_of_v<RenderGraphBase, Derived>);
		if constexpr (std::is_base_of_v<RGObjectBase, Derived>)
		    m_descriptor_set_data.set_render_graph_ptr(
		        ((RGObjectBase *)static_cast<Derived *>(this))->GetRenderGraphPtr());
		else
		    m_descriptor_set_data.set_render_graph_ptr((RenderGraphBase *)static_cast<Derived *>(this)); */
	}
	inline RGAttachmentInputSlot(RGAttachmentInputSlot &&) noexcept = default;
	inline ~RGAttachmentInputSlot() = default;

protected:
	inline const RGAttachmentData &GetAttachmentData() const { return m_attachment_data; }

	template <uint32_t Index, RGUsage Usage, typename = std::enable_if_t<kRGUsageIsColorAttachment<Usage>>>
	inline bool AddColorAttachmentInput(const RGPoolKey &input_key, RGImageBase *image) {
		static_assert(kRGUsageHasSpecifiedPipelineStages<Usage>);

		assert(!m_attachment_data.IsColorAttachmentExist(Index));
		if (m_attachment_data.IsColorAttachmentExist(Index))
			return false;
		auto input = get_input_pool_ptr()->add_input(input_key, image, Usage, kRGUsageGetSpecifiedPipelineStages<Usage>,
		                                             UINT32_MAX, Index);
		assert(input);
		if (!input)
			return false;
		m_attachment_data.AddColorAttachment(Index, input);
		return true;
	}

	template <uint32_t AttachmentIndex, uint32_t DescriptorBinding>
	inline bool AddInputAttachmentInput(const RGPoolKey &input_key, RGImageBase *image) {
		static_assert(kRGUsageHasSpecifiedPipelineStages<RGUsage::kInputAttachment>);

		assert(!m_attachment_data.IsInputAttachmentExist(AttachmentIndex));
		if (m_attachment_data.IsInputAttachmentExist(AttachmentIndex))
			return false;
		auto input = get_descriptor_slot_ptr()->add_input_descriptor(
		    input_key, image, RGUsage::kInputAttachment, kRGUsageGetSpecifiedPipelineStages<RGUsage::kInputAttachment>,
		    DescriptorBinding, nullptr, AttachmentIndex);
		assert(input);
		if (!input)
			return false;
		m_attachment_data.AddInputAttachment(AttachmentIndex, input);
		return true;
	}

	template <RGUsage Usage, typename = std::enable_if_t<kRGUsageIsDepthAttachment<Usage>>>
	inline bool SetDepthAttachmentInput(const RGPoolKey &input_key, RGImageBase *image) {
		static_assert(kRGUsageHasSpecifiedPipelineStages<Usage>);

		assert(!m_attachment_data.IsDepthAttachmentExist());
		if (m_attachment_data.IsDepthAttachmentExist())
			return false;
		auto input =
		    get_input_pool_ptr()->add_input(input_key, image, Usage, kRGUsageGetSpecifiedPipelineStages<Usage>);
		assert(input);
		if (!input)
			return false;
		m_attachment_data.SetDepthAttachment(input);
		return true;
	}
};

template <typename Derived> void RGInputPool<Derived>::RemoveInput(const RGPoolKey &input_key) {
	if constexpr (std::is_base_of_v<RGDescriptorInputSlot<Derived>, Derived> ||
	              std::is_base_of_v<RGAttachmentInputSlot<Derived>, Derived>) {
		const RGInput *input = GetInput(input_key);
		if constexpr (std::is_base_of_v<RGDescriptorInputSlot<Derived>, Derived>)
			((RGDescriptorInputSlot<Derived> *)static_cast<Derived *>(this))->pre_remove_input(input);
		if constexpr (std::is_base_of_v<RGAttachmentInputSlot<Derived>, Derived>)
			((RGAttachmentInputSlot<Derived> *)static_cast<Derived *>(this))->pre_remove_input(input);
	}
	InputPool::Delete(input_key);
}

template <typename Derived> void RGInputPool<Derived>::ClearInputs() {
	if constexpr (std::is_base_of_v<RGDescriptorInputSlot<Derived>, Derived>)
		((RGDescriptorInputSlot<Derived> *)static_cast<Derived *>(this))->pre_clear_inputs();
	if constexpr (std::is_base_of_v<RGAttachmentInputSlot<Derived>, Derived>)
		((RGAttachmentInputSlot<Derived> *)static_cast<Derived *>(this))->pre_clear_inputs();
	InputPool::Clear();
}

#pragma endregion

/////////////////////////
// SECTION Render Pass //
/////////////////////////
#pragma region SECTION : Render Pass

namespace _details_rg_pool_ {
using PassPoolData = PoolData<RGPassBase>;
}

class RGPassBase : public RGObjectBase {
private:
	const _details_rg_pool_::InputPoolData *m_p_input_pool_data{};
	// const _details_rg_pool_::ResourcePoolData *m_p_resource_pool_data{};
	// const _details_rg_pool_::PassPoolData *m_p_pass_pool_data{};
	const RGDescriptorSetData *m_p_descriptor_set_data{};
	const RGAttachmentData *m_p_attachment_data{};

	mutable struct {
#ifndef NDEBUG
		bool in_stack;
#endif
		uint32_t in_degree;
	} m_traversal_data{};

	template <typename, uint8_t> friend class RGPass;
	friend class RenderGraphBase;

public:
	inline RGPassBase() = default;
	inline RGPassBase(RGPassBase &&) noexcept = default;
	inline ~RGPassBase() override = default;

	virtual void CmdExecute(const Ptr<CommandBuffer> &command_buffer) = 0;
};

template <typename Derived> class RGPassPool : public RGPool<Derived, RGPassBase> {
private:
	using PassPool = RGPool<Derived, RGPassBase>;

public:
	inline RGPassPool() = default;
	inline RGPassPool(RGPassPool &&) noexcept = default;
	inline ~RGPassPool() override = default;

protected:
	template <typename PassType, typename... Args, typename = std::enable_if_t<std::is_base_of_v<RGPassBase, PassType>>>
	inline PassType *CreatePass(const RGPoolKey &pass_key, Args &&...args) {
		return PassPool::template CreateAndInitialize<0, PassType, Args...>(pass_key, std::forward<Args>(args)...);
	}
	inline void DeletePass(const RGPoolKey &pass_key) { return PassPool::Delete(pass_key); }

	template <typename PassType = RGPassBase, typename = std::enable_if_t<std::is_base_of_v<RGPassBase, PassType> ||
	                                                                      std::is_same_v<RGPassBase, PassType>>>
	inline PassType *GetPass(const RGPoolKey &pass_key) const {
		return PassPool::template Get<0, PassType>(pass_key);
	}
	inline void ClearPasses() { PassPool::Clear(); }
};

namespace _details_rg_pass_ {
struct NoResourcePool {};
struct NoPassPool {};
struct NoAliasOutputPool {};
struct NoDescriptorInputSlot {};
struct NoAttachmentInputSlot {};
} // namespace _details_rg_pass_

struct RGPassFlag {
	enum : uint8_t { kEnableResource = 1u, kEnableSubpass = 2u, kDescriptor = 4u, kGraphics = 8u, kAsyncCompute = 16u };
};

template <typename Derived, uint8_t Flags>
class RGPass : public RGPassBase,
               public RGInputPool<Derived>,
               public std::conditional_t<(Flags & RGPassFlag::kEnableResource) != 0, RGResourcePool<Derived>,
                                         _details_rg_pass_::NoResourcePool>,
               public std::conditional_t<(Flags & RGPassFlag::kDescriptor) != 0, RGDescriptorInputSlot<Derived>,
                                         _details_rg_pass_::NoDescriptorInputSlot>,
               public std::conditional_t<(Flags & RGPassFlag::kGraphics) != 0, RGAttachmentInputSlot<Derived>,
                                         _details_rg_pass_::NoAttachmentInputSlot>,
               public std::conditional_t<(Flags & RGPassFlag::kEnableSubpass) != 0, RGPassPool<Derived>,
                                         _details_rg_pass_::NoPassPool>,
               public std::conditional_t<(Flags & RGPassFlag::kEnableSubpass) != 0, RGAliasOutputPool<Derived>,
                                         _details_rg_pass_::NoAliasOutputPool> {
public:
	inline RGPass() {
		m_p_input_pool_data = &RGInputPool<Derived>::GetPoolData();
		/* if constexpr ((Flags & RGPassFlag::kEnableResourceAllocation) != 0)
		    m_p_resource_pool_data = &RGResourcePool<Derived>::GetPoolData();
		if constexpr ((Flags & RGPassFlag::kEnableSubpass) != 0)
		    m_p_pass_pool_data = &RGPassPool<Derived>::GetPoolData(); */
		if constexpr ((Flags & RGPassFlag::kDescriptor) != 0)
			m_p_descriptor_set_data = &RGDescriptorInputSlot<Derived>::GetDescriptorSetData();
		if constexpr ((Flags & RGPassFlag::kGraphics) != 0)
			m_p_attachment_data = &RGAttachmentInputSlot<Derived>::GetAttachmentData();
	}
	inline RGPass(RGPass &&) noexcept = default;
	inline ~RGPass() override = default;
};

#pragma endregion

//////////////////////////
// SECTION Render Graph //
//////////////////////////
#pragma region SECTION : Render Graph

namespace _details_rg_pool_ {
using ResultPoolData = PoolData<RGResourceBase *>;
}
template <typename Derived> class RGResultPool : public RGPool<Derived, RGResourceBase *> {
private:
	using ResultPool = RGPool<Derived, RGResourceBase *>;

public:
	inline RGResultPool() = default;
	inline RGResultPool(RGResultPool &&) noexcept = default;
	inline ~RGResultPool() override = default;

protected:
	inline bool AddResult(const RGPoolKey &result_key, RGResourceBase *resource) {
		assert(resource);
		return ResultPool::template CreateAndInitialize<0, RGResourceBase *>(result_key, resource);
	}
	inline void RemoveResult(const RGPoolKey &result_key) { ResultPool::Delete(result_key); }
	inline void ClearResults() { ResultPool::Clear(); }
};

class RenderGraphBase : public DeviceObjectBase {
private:
	Ptr<Device> m_device_ptr;
	uint32_t m_frame_count{};
	VkExtent2D m_canvas_size{};

	const _details_rg_pool_::ResultPoolData *m_p_result_pool_data{};
	// const _details_rg_pool_::ResourcePoolData *m_p_resource_pool_data{};
	// const _details_rg_pool_::PassPoolData *m_p_pass_pool_data{};

	mutable std::vector<RGPassBase *> m_pass_graph_nodes;
	void pass_graph_traverse(RGPassBase *pass) const;

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
	inline const Ptr<Device> &GetDevicePtr() const final { return m_device_ptr; }
	inline uint32_t GetFrameCount() const { return m_frame_count; }
	void DebugTraverse() const;
};

template <typename Derived>
class RenderGraph : public RenderGraphBase,
                    public RGPassPool<Derived>,
                    public RGResourcePool<Derived>,
                    public RGResultPool<Derived> {
public:
	template <typename... Args> inline static Ptr<Derived> Create(const Ptr<Device> &device_ptr, Args &&...args) {
		static_assert(std::is_base_of_v<RenderGraph<Derived>, Derived>);

		auto ret = std::make_shared<Derived>(std::forward<Args>(args)...);
		dynamic_cast<RenderGraphBase *>(ret.get())->m_device_ptr = device_ptr;
		return ret;
	}
	inline RenderGraph() {
		m_p_result_pool_data = &RGResultPool<Derived>::GetPoolData();
		// m_p_resource_pool_data = &RGResourcePool<Derived>::GetPoolData();
		// m_p_pass_pool_data = &RGPassPool<Derived>::GetPoolData();
	}
};

#pragma endregion

// TODO: Debug Type Traits
static_assert(std::is_same_v<RGPoolVariant<RGBufferAlias, RGObjectBase, RGResourceBase, RGImageAlias>,
                             std::variant<std::monostate, RGImageAlias, std::unique_ptr<RGResourceBase>,
                                          std::unique_ptr<RGObjectBase>, RGBufferAlias>>);
} // namespace myvk::render_graph

#endif
