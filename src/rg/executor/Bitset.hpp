//
// Created by adamyuan on 2/6/24.
//

#pragma once
#ifndef MYVK_RG_EXE_BITSET_HPP
#define MYVK_RG_EXE_BITSET_HPP

#include <cinttypes>

namespace myvk_rg::executor {

inline constexpr std::size_t BitsetSize(std::size_t bit_count) {
	return (bit_count >> 6u) + ((bit_count & 0x3f) ? 1u : 0u);
}
inline constexpr bool BitsetGet(const uint64_t *data, std::size_t bit_pos) {
	return data[bit_pos >> 6u] & (1ull << (bit_pos & 0x3fu));
}
inline constexpr void BitsetAdd(uint64_t *data, std::size_t bit_pos) {
	data[bit_pos >> 6u] |= (1ull << (bit_pos & 0x3fu));
}

} // namespace myvk_rg::executor

#endif // MYVK_RG_EXE_BITSET_HPP
