#pragma once
#ifndef MYVK_RG_DEF_EXE_ERROR_HPP
#define MYVK_RG_DEF_EXE_ERROR_HPP

#include <myvk_rg/interface/Alias.hpp>
#include <myvk_rg/interface/Key.hpp>
#include <stdexcept>
#include <variant>

namespace default_executor {

using namespace myvk_rg::interface;

namespace error {

struct NullResource {
	GlobalKey parent;
	inline std::string Format() const { return "Null Resource in " + parent.Format(); }
};
struct NullInput {
	GlobalKey parent;
	inline std::string Format() const { return "Null Input in " + parent.Format(); }
};
struct NullPass {
	GlobalKey parent;
	inline std::string Format() const { return "Null Pass in " + parent.Format(); }
};
struct ResourceNotFound {
	GlobalKey key;
	inline std::string Format() const { return "Resource " + key.Format() + " not found"; }
};
struct InputNotFound {
	GlobalKey key;
	inline std::string Format() const { return "Input " + key.Format() + " not found"; }
};
struct PassNotFound {
	GlobalKey key;
	inline std::string Format() const { return "Pass " + key.Format() + " not found"; }
};
struct AliasNoMatch {
	AliasBase alias;
	ResourceType actual_type;
	inline std::string Format() const {
		return "Alias source " + alias.GetSourceKey().Format() + " is not matched with type " +
		       std::to_string(static_cast<int>(actual_type));
	}
};
struct WriteToLastFrame {
	AliasBase alias;
	GlobalKey pass_key;
	inline std::string Format() const {
		return "Write to last frame source " + alias.GetSourceKey().Format() + " in pass " + pass_key.Format();
	}
};
struct MultipleWrite {
	AliasBase alias;
	inline std::string Format() const {
		return "Alias souce " + alias.GetSourceKey().Format() + " is written multiple times";
	}
};
struct PassNotDAG {
	inline std::string Format() const { return "Pass cycle dependencies in Render Graph"; }
};
struct ResourceNotTree {
	inline std::string Format() const { return "Resources are not tree structured"; }
};
struct ResourceLFParent {
	GlobalKey key;
	inline std::string Format() const {
		return "Last frame resource " + key.Format() + " is referenced by another resource";
	}
};
struct ResourceExtParent {
	GlobalKey key;
	inline std::string Format() const {
		return "External resource " + key.Format() + " is referenced by another resource";
	}
};
struct ImageNotMerge {
	GlobalKey key;
	inline std::string Format() const { return "Image " + key.Format() + " failed to merge"; }
};

template <typename Error> struct Exception : public std::exception {
	Error error;
	explicit Exception(Error error) : error{std::move(error)} {}
	inline const char *what() const noexcept override { return error.Format().c_str(); }
};
template <typename Error> void Throw(Error &&error) {
	throw Exception<Error>{std::forward<Error>(error)};
	// TODO: abort() if exception is disabled
}

} // namespace error

} // namespace default_executor

#endif