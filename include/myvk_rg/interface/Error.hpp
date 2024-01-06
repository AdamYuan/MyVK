#pragma once
#ifndef MYVK_RG_ERROR_HPP
#define MYVK_RG_ERROR_HPP

#include "Key.hpp"
#include <stdexcept>
#include <variant>

namespace myvk_rg::interface {

namespace error {

struct NullResource {
	GlobalKey key;
	inline std::string Format() const { return "Null Resource under " + key.Format(); }
};
struct NullInput {
	GlobalKey key;
	inline std::string Format() const { return "Null Input under " + key.Format(); }
};
struct NullPass {
	GlobalKey key;
	inline std::string Format() const { return "Null Pass under " + key.Format(); }
};

} // namespace error

template <typename... Errors> class Error {
private:
	std::variant<Errors...> m_err;

public:
	template <typename T> inline Error(T &&val) : m_err{std::forward<T>(val)} {}
	inline std::string Format() const {
		return std::visit([](const auto &err) -> std::string { return err.Format(); }, m_err);
	}
	template <typename Visitor> inline void Visit(Visitor &&visitor) const {
		std::visit(std::forward<Visitor>(visitor), m_err);
	}
};

using CompileError = Error<error::NullResource, error::NullInput, error::NullPass>;

template <typename Type, typename ErrorType> class Result {
private:
	using RType = std::conditional_t<std::is_same_v<Type, void>, std::monostate, Type>;
	std::variant<RType, ErrorType> m_res;

public:
	inline Result() : m_res{std::monostate{}} { static_assert(std::is_same_v<Type, void>); }
	template <typename T> inline Result(T &&val) : m_res{std::forward<T>(val)} {}
	inline bool IsError() const { return m_res.index() == 1; }
	inline bool IsOK() const { return m_res.index() == 0; }
	inline RType PopValue() {
		return std::visit(
		    [](auto &v) -> RType {
			    if constexpr (std::is_same_v<RType, std::decay_t<decltype(v)>>)
				    return std::move(v);
			    else
				    throw std::runtime_error("Result has no value");
		    },
		    m_res);
	}
	inline ErrorType PopError() {
		return std::visit(
		    [](auto &v) -> ErrorType {
			    if constexpr (std::is_same_v<RType, std::decay_t<decltype(v)>>)
				    throw std::runtime_error("Result has no error");
			    else
				    return std::move(v);
		    },
		    m_res);
	}
};

template <typename Type> using CompileResult = Result<Type, CompileError>;

} // namespace myvk_rg::interface

#endif
