#pragma once
#ifndef MYVK_RG_ERRORMACRO_HPP
#define MYVK_RG_ERRORMACRO_HPP

#include <myvk_rg/interface/Error.hpp>

#define UNWRAP_ASSIGN(L_VALUE, RESULT) \
	do { \
		auto result = RESULT; \
		if (result.IsError()) \
			return result.PopError(); \
		L_VALUE = result.PopValue(); \
	} while (false)
#define UNWRAP(RESULT) \
	do { \
		auto result = RESULT; \
		if (result.IsError()) \
			return result.PopError(); \
	} while (false)

#endif
