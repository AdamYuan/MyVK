//
// Created by adamyuan on 2/6/24.
//

#pragma once
#ifndef MYVK_RG_EXE_BITSET_HPP
#define MYVK_RG_EXE_BITSET_HPP

#include <bit>
#include <cinttypes>
#include <ranges>

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

class Bitset {
private:
	std::size_t m_count{}, m_size{};
	std::vector<uint64_t> m_bits{};

public:
	inline Bitset() = default;
	inline explicit Bitset(std::size_t count) { Reset(count); }
	inline void Reset(std::size_t count) {
		m_count = count;
		m_size = BitsetSize(count);
		m_bits.clear();
		m_bits.resize(count);
	}
	inline void Add(std::size_t x) { BitsetAdd(m_bits.data(), x); }
	inline bool Get(std::size_t x) const { return BitsetGet(m_bits.data(), x); }

	inline uint64_t *GetData() { return m_bits.data(); }
	inline const uint64_t *GetData() const { return m_bits.data(); }

	inline auto GetBits() const {
		return std::views::iota(std::size_t{0}, m_count) | std::views::filter([this](std::size_t x) { return Get(x); });
	}
	inline std::size_t GetFirstBit() const {
		std::size_t base = 0;
		for (uint64_t b64 : m_bits)
			if (b64)
				return base + std::countr_zero(b64);
			else
				base += 64;
		return m_count;
	}
};

} // namespace myvk_rg::executor

#endif // MYVK_RG_EXE_BITSET_HPP
