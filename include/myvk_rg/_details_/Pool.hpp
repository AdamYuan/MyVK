#ifndef MYVK_RG_POOL_HPP
#define MYVK_RG_POOL_HPP

#include "Macro.hpp"
#include "ObjectBase.hpp"

#include <cinttypes>
#include <cstdio>
#include <memory>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <variant>

namespace myvk_rg::_details_ {

// Pool
namespace _details_rg_pool_ {

// Value Wrapper
template <typename Type> class Value {
private:
	template <typename T> struct Storage {
		alignas(T) std::byte m_buffer[sizeof(T)]{};
		inline T &Get() const { return *(T *)(m_buffer); }
	};

	inline constexpr static bool kUsePtr = !std::is_final_v<Type>;
	std::conditional_t<kUsePtr, std::unique_ptr<Type>, Storage<Type>> m_value;

public:
	template <typename TypeToCons>
	inline constexpr static bool kCanConstruct =
	    kUsePtr ? std::is_constructible_v<Type *, TypeToCons *> : std::is_same_v<Type, TypeToCons>;
	template <typename TypeToGet>
	inline constexpr static bool kCanGet =
	    kUsePtr ? (std::is_base_of_v<Type, TypeToGet> || std::is_base_of_v<TypeToGet, Type> ||
	               std::is_same_v<Type, TypeToGet>)
	            : (std::is_base_of_v<TypeToGet, Type> || std::is_same_v<Type, TypeToGet>);

	template <typename TypeToCons, typename... Args, typename = std::enable_if_t<kCanConstruct<TypeToCons>>>
	inline TypeToCons *Construct(Args &&...args) {
		if constexpr (kUsePtr) {
			auto ptr = std::make_unique<TypeToCons>(std::forward<Args>(args)...);
			TypeToCons *ret = ptr.get();
			m_value = std::move(ptr);
			return ret;
		} else {
			m_value.Get() = TypeToCons(std::forward<Args>(args)...);
			return &m_value.Get();
		}
	}
	template <typename TypeToGet, typename = std::enable_if_t<kCanGet<TypeToGet>>> inline TypeToGet *Get() const {
		Type *ptr;
		if constexpr (kUsePtr)
			ptr = m_value.get();
		else
			ptr = &m_value.Get();

		if constexpr (std::is_same_v<Type, TypeToGet>)
			return (TypeToGet *)ptr;
		else
			return (TypeToGet *)dynamic_cast<const TypeToGet *>(ptr);
	}
};

// Variant Wrapper
template <typename... Types> class Variant {
private:
	std::variant<Value<Types>...> m_variant;

	template <typename TypeToCons, size_t I = 0> inline static constexpr size_t GetConstructIndex() {
		if constexpr (I >= sizeof...(Types)) {
			return -1;
		} else {
			if constexpr (Value<std::tuple_element_t<I, std::tuple<Types...>>>::template kCanConstruct<TypeToCons>)
				return I;
			else
				return (GetConstructIndex<TypeToCons, I + 1>());
		}
	}
	template <typename TypeToGet, size_t I = 0> inline static constexpr bool CanGet() {
		if constexpr (I >= sizeof...(Types))
			return false;
		else {
			if constexpr (Value<std::tuple_element_t<I, std::tuple<Types...>>>::template kCanGet<TypeToGet>)
				return true;
			return CanGet<TypeToGet, I + 1>();
		}
	}

public:
	template <typename TypeToCons> inline constexpr static bool kCanConstruct = GetConstructIndex<TypeToCons>() != -1;
	template <typename TypeToGet> inline constexpr static bool kCanGet = CanGet<TypeToGet>();

	template <typename TypeToCons, typename... Args, typename = std::enable_if_t<kCanConstruct<TypeToCons>>>
	inline TypeToCons *Construct(Args &&...args) {
		constexpr auto kIndex = GetConstructIndex<TypeToCons>();
		using V = Value<std::tuple_element_t<kIndex, std::tuple<Types...>>>;
		m_variant = V{};
		return std::visit(
		    [&](auto &v) -> TypeToCons * {
			    if constexpr (std::decay_t<decltype(v)>::template kCanConstruct<TypeToCons>)
				    return v.template Construct<TypeToCons>(std::forward<Args>(args)...);
			    else
				    return nullptr;
		    },
		    m_variant);
	}
	template <typename TypeToGet, typename = std::enable_if_t<kCanGet<TypeToGet>>> inline TypeToGet *Get() {
		return std::visit(
		    [](const auto &v) -> TypeToGet * {
			    if constexpr (std::decay_t<decltype(v)>::template kCanGet<TypeToGet>)
				    return v.template Get<TypeToGet>();
			    else
				    return nullptr;
		    },
		    m_variant);
	}
};

// Wrapper
template <typename Type> struct WrapperAux {
	using T = Value<Type>;
};
template <typename... Types> struct WrapperAux<std::variant<Types...>> {
	using T = Variant<Types...>;
};
template <typename Type> using Wrapper = typename WrapperAux<Type>::T;

// Wrapper Tuple
template <typename... Types> class WrapperTuple {
private:
	std::tuple<Wrapper<Types>...> m_tuple;

public:
	template <std::size_t Index, typename TypeToCons, typename... Args> inline TypeToCons *Construct(Args &&...args) {
		return std::get<Index>(m_tuple).template Construct<TypeToCons>(std::forward<Args>(args)...);
	}
	template <std::size_t Index, typename TypeToGet> inline TypeToGet *Get() const {
		return std::get<Index>(m_tuple).template Get<TypeToGet>();
	}
};

// Pool Data
template <typename... Types> using PoolData = std::unordered_map<PoolKey, WrapperTuple<Types...>, PoolKey::Hash>;

} // namespace _details_rg_pool_

template <typename Derived, typename... Types> class Pool {
private:
	using PoolData = _details_rg_pool_::PoolData<Types...>;
	PoolData m_data;

public:
	inline Pool() = default;
	inline Pool(Pool &&) noexcept = default;
	inline virtual ~Pool() = default;

protected:
	inline const PoolData &GetPoolData() const { return m_data; }

	template <std::size_t Index, typename TypeToCons, typename... Args>
	inline TypeToCons *Construct(const PoolKey &key, Args &&...args) {
		auto it = m_data.insert({key, _details_rg_pool_::WrapperTuple<Types...>{}});
		if constexpr (std::is_base_of_v<ObjectBase, TypeToCons>) {
			ObjectBase::Parent parent{.p_pool_key = &it.first};
			if constexpr (std::is_base_of_v<RenderGraphBase, Derived>)
				parent.p_var_parent = (RenderGraphBase *)static_cast<const Derived *>(this);
			else if constexpr (std::is_base_of_v<ObjectBase, Derived>)
				parent.p_var_parent = (ObjectBase *)static_cast<const Derived *>(this);
			else
				static_assert(false);

			return it->second.template Construct<Index, TypeToCons>(parent, std::forward<Args>(args)...);
		} else
			return it->second.template Construct<Index, TypeToCons>(std::forward<Args>(args)...);
	}
	inline bool Exist(const PoolKey &key) const { return m_data.count(key); }
	inline void Delete(const PoolKey &key) { m_data.pool.erase(key); }
	template <std::size_t Index, typename Type> inline Type *Get(const PoolKey &key) const {
		auto it = m_data.find(key);
		return it == m_data.end() ? nullptr : it->second.template Get<Index, Type>();
	}
	inline void Clear() { m_data.clear(); }
};

} // namespace myvk_rg::_details_

#endif
