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
template <typename VariantType, typename T, size_t I = 0> constexpr size_t GetVariantIndex() {
	if constexpr (I >= std::variant_size_v<VariantType>) {
		return -1;
	} else {
		using VTI = std::variant_alternative_t<I, VariantType>;
		if constexpr (std::is_constructible_v<VTI, T &&> || std::is_same_v<VTI, T>)
			return I;
		else
			return (GetVariantIndex<VariantType, T, I + 1>());
	}
}
template <typename VariantType, typename T> constexpr bool kVariantCanHold = GetVariantIndex<VariantType, T>() != -1;

template <typename Type> struct TypeTraits {
	constexpr static bool kIsRGObject = std::is_base_of_v<RGObjectBase, Type>;
	constexpr static bool kIsRGVariant = false;
	constexpr static bool kAlterPtr = (kIsRGObject && !std::is_final_v<Type>) || !std::is_move_constructible_v<Type>;
	constexpr static bool kAlterOptional = !kAlterPtr && (kIsRGObject || !std::is_default_constructible_v<Type>);
	constexpr static bool kAlterNone = !kAlterPtr && !kAlterOptional;

	using AlterType = std::conditional_t<kAlterPtr, std::unique_ptr<Type>,
	                                     std::conditional_t<kAlterOptional, std::optional<Type>, Type>>;
	using VariantAlterType = std::conditional_t<kAlterPtr, std::unique_ptr<Type>, Type>;

	template <typename X>
	constexpr static bool Match = kAlterPtr ? std::is_base_of_v<Type, X> : std::is_same_v<Type, X>;

	template <typename ConsType, typename... Args, typename = std::enable_if_t<Match<ConsType>>>
	inline static ConsType *Initialize(AlterType &val, Args &&...args) {
		if constexpr (kAlterPtr) {
			auto uptr = std::make_unique<ConsType>(std::forward<Args>(args)...);
			ConsType *ret = uptr.get();
			val = std::move(uptr);
			return ret;
		} else if constexpr (kAlterOptional) {
			val.emplace(std::forward<Args>(args)...);
			return &(val.value());
		} else {
			val = ConsType(std::forward<Args>(args)...);
			return &val;
		}
	}
	inline static bool IsInitialized(const AlterType &val) {
		if constexpr (kAlterPtr)
			return val;
		else if constexpr (kAlterOptional)
			return val.has_value();
		return true;
	}
	template <typename GetType, typename = std::enable_if_t<Match<GetType>>>
	inline static GetType *Get(const AlterType &val) {
		if constexpr (kAlterPtr)
			return (GetType *)dynamic_cast<const GetType *>(val.get());
		else if constexpr (kAlterOptional)
			return val.has_value() ? (GetType *)(&(val.value())) : nullptr;
		return (GetType *)(&val);
	}
};
template <typename... VariantArgs> struct TypeTraits<std::variant<VariantArgs...>> {
private:
	using Type = std::variant<VariantArgs...>;

public:
	constexpr static bool kIsRGObject = false;
	constexpr static bool kIsRGVariant = true;

	constexpr static bool kAlterPtr = false;
	constexpr static bool kAlterOptional = false;
	constexpr static bool kAlterNone = true;

	using AlterType = Type;
	template <typename X>
	constexpr static bool Match = kVariantCanHold<Type, X> || kVariantCanHold<Type, std::unique_ptr<X>>;

	template <typename ConsType, typename... Args, typename = std::enable_if_t<Match<ConsType>>>
	inline static ConsType *Initialize(AlterType &val, Args &&...args) {
		if constexpr (kVariantCanHold<Type, ConsType>) {
			constexpr size_t kIndex = GetVariantIndex<Type, ConsType>();
			// If Don't need Pointer, prefer plain type
			printf("index = %lu\n", kIndex);
			val.template emplace<kIndex>(ConsType(std::forward<Args>(args)...));
			return &(std::get<kIndex>(val));
		} else {
			constexpr size_t kPtrIndex = GetVariantIndex<Type, std::unique_ptr<ConsType>>();

			auto uptr = std::make_unique<ConsType>(std::forward<Args>(args)...);
			ConsType *ret = uptr.get();
			val.template emplace<kPtrIndex>(std::move(uptr));
			return ret;
		}
	}
	inline static bool IsInitialized(const AlterType &val) { return val.index(); }
	template <typename TypeToGet, typename = std::enable_if_t<Match<TypeToGet>>>
	inline static TypeToGet *Get(const AlterType &val) {
		if constexpr (kVariantCanHold<Type, TypeToGet>) {
			constexpr size_t kIndex = GetVariantIndex<Type, TypeToGet>();
			return val.index() == kIndex ? (TypeToGet *)(&std::get<kIndex>(val)) : nullptr;
		} else {
			constexpr size_t kPtrIndex = GetVariantIndex<Type, std::unique_ptr<TypeToGet>>();
			return val.index() == kPtrIndex ? (TypeToGet *)(std::get<kPtrIndex>(val).get()) : nullptr;
		}
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
	static_assert(TypeTraits<RGType>::kIsRGObject);
	using T =
	    typename VariantCat<typename TypeTraits<RGType>::VariantAlterType, typename TypeVariant<RGOthers...>::T>::T;
};
template <> struct TypeVariant<> {
	using T = std::monostate;
};
} // namespace _details_rg_object_pool_
template <typename... RGTypes> using RGObjectVariant = typename _details_rg_object_pool_::TypeVariant<RGTypes...>::T;
template <typename RGDerived, typename... Types> class RGObjectPool {
private:
	using TypeTuple = typename _details_rg_object_pool_::TypeTuple<Types...>::T;
	template <std::size_t Index> using GetType = std::tuple_element_t<Index, std::tuple<Types...>>;
	template <std::size_t Index> using GetAlterType = std::tuple_element_t<Index, TypeTuple>;
	template <std::size_t Index>
	static constexpr bool kAlterPtr = _details_rg_object_pool_::TypeTraits<GetType<Index>>::kAlterPtr;
	template <std::size_t Index>
	static constexpr bool kAlterOptional = _details_rg_object_pool_::TypeTraits<GetType<Index>>::kAlterOptional;
	template <std::size_t Index>
	static constexpr bool kIsRGObject = _details_rg_object_pool_::TypeTraits<GetType<Index>>::kIsRGObject;
	template <std::size_t Index>
	static constexpr bool kIsRGVariant = _details_rg_object_pool_::TypeTraits<GetType<Index>>::kIsRGVariant;
	template <std::size_t Index, typename T>
	static constexpr bool kTypeMatch = _details_rg_object_pool_::TypeTraits<GetType<Index>>::template Match<T>;

	RGKeyMap<TypeTuple> m_pool;

	template <std::size_t Index, typename ConsType, typename... Args, typename MapIterator,
	          typename = std::enable_if_t<kTypeMatch<Index, ConsType>>>
	inline ConsType *initialize(const MapIterator &it, Args &&...args) {
		GetAlterType<Index> &ref = std::get<Index>(it->second);

		using Type = GetType<Index>;
		ConsType *ptr =
		    _details_rg_object_pool_::TypeTraits<Type>::template Initialize<ConsType>(ref, std::forward<Args>(args)...);
		// Initialize RGObjectBase
		if constexpr (std::is_base_of_v<RGObjectBase, ConsType>) {
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
		if constexpr (std::is_base_of_v<RGResourceBase, ConsType>) {
			auto resource_ptr = static_cast<RGResourceBase *>(ptr);
			if constexpr (std::is_base_of_v<RGPassBase, RGDerived>)
				resource_ptr->set_producer_pass_ptr((RGPassBase *)static_cast<const RGDerived *>(this));
		}
		return ptr;
	}

	template <std::size_t Index, typename MapIterator> inline bool is_initialized(const MapIterator &it) const {
		const GetAlterType<Index> &ref = std::get<Index>(it->second);
		using Type = GetType<Index>;
		return _details_rg_object_pool_::TypeTraits<Type>::IsInitialized(ref);
	}

	template <std::size_t Index, typename TypeToGet, typename MapIterator,
	          typename = std::enable_if_t<kTypeMatch<Index, TypeToGet>>>
	inline TypeToGet *get(const MapIterator &it) const {
		const GetAlterType<Index> &ref = std::get<Index>(it->second);
		using Type = GetType<Index>;
		return _details_rg_object_pool_::TypeTraits<Type>::template Get<TypeToGet>(ref);
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
	template <std::size_t Index, typename ConsType, typename... Args,
	          typename = std::enable_if_t<kTypeMatch<Index, ConsType>>>
	inline ConsType *CreateAndInitialize(const RGObjectPoolKey &key, Args &&...args) {
		if (m_pool.find(key) != m_pool.end())
			return nullptr;
		auto it = m_pool.insert({key, TypeTuple{}}).first;
		return initialize<Index, ConsType, Args...>(it, std::forward<Args>(args)...);
	}
	// Create Tag Only
	inline void Create(const RGObjectPoolKey &key) {
		if (m_pool.find(key) != m_pool.end())
			return;
		m_pool.insert(std::make_pair(key, TypeTuple{}));
	}
	// Initialize Object of a Tag
	template <std::size_t Index, typename ConsType, typename... Args,
	          typename = std::enable_if_t<kTypeMatch<Index, ConsType>>>
	inline ConsType *Initialize(const RGObjectPoolKey &key, Args &&...args) {
		auto it = m_pool.find(key);
		if (it == m_pool.end())
			return nullptr;
		return initialize<Index, ConsType, Args...>(it, std::forward<Args>(args)...);
	}
	// Get an Object from a Tag, if not Initialized, Initialize it.
	template <std::size_t Index, typename ConsType, typename... Args,
	          typename = std::enable_if_t<kTypeMatch<Index, ConsType> && (kIsRGObject<Index> || kIsRGVariant<Index>)>>
	inline ConsType *InitializeOrGet(const RGObjectPoolKey &key, Args &&...args) {
		auto it = m_pool.find(key);
		if (it == m_pool.end())
			return nullptr;
		return is_initialized<Index>(it) ? (ConsType *)get<Index, ConsType>(it)
		                                 : initialize<Index, ConsType, Args...>(it, std::forward<Args>(args)...);
	}
	// Delete a Tag and its Objects
	inline void Delete(const RGObjectPoolKey &key) { m_pool.erase(key); }
	// Get an Object from a Tag
	template <std::size_t Index, typename Type, typename = std::enable_if_t<kTypeMatch<Index, Type>>>
	inline Type *Get(const RGObjectPoolKey &key) const {
		auto it = m_pool.find(key);
		if (it == m_pool.end())
			return nullptr;
		return (Type *)get<Index, Type>(it);
	}
};
