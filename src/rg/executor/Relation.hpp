//
// Created by adamyuan on 2/6/24.
//

#pragma once
#ifndef MYVK_RG_EXE_RELATION_HPP
#define MYVK_RG_EXE_RELATION_HPP

#include <cinttypes>
#include <vector>

inline constexpr uint32_t BitsetSize(uint32_t bit_count) { return (bit_count >> 6u) + ((bit_count & 0x3f) ? 1u : 0u); }
inline constexpr bool BitsetGet(const uint64_t *data, uint32_t bit_pos) {
	return data[bit_pos >> 6u] & (1ull << (bit_pos & 0x3fu));
}
inline constexpr void BitsetSet(uint64_t *data, uint32_t bit_pos) {
	data[bit_pos >> 6u] |= (1ull << (bit_pos & 0x3fu));
}

class Relation {
private:
	uint32_t m_count_l{}, m_count_r{};
	uint32_t m_size_r{};
	std::vector<uint64_t> m_bit_matrix;

public:
	inline Relation() = default;
	inline Relation(uint32_t count_l, uint32_t count_r) { Reset(count_l, count_r); }
	inline void Reset(uint32_t count_l, uint32_t count_r) {
		m_count_l = count_l, m_count_r = count_r;
		m_size_r = BitsetSize(count_r);
		m_bit_matrix.clear();
		m_bit_matrix.resize(count_l * m_size_r);
	}
	inline void Set(uint32_t l, uint32_t r) { BitsetSet(GetRowData(l), r); }
	inline void Apply(uint32_t l_from, uint32_t l_to) {
		for (uint32_t i = 0; i < m_size_r; ++i)
			GetRowData(l_to)[i] |= GetRowData(l_from)[i];
	}
	inline void Apply(const Relation &src_relation, uint32_t l_from, uint32_t l_to) {
		assert(m_size_r == src_relation.m_size_r);
		for (uint32_t i = 0; i < m_size_r; ++i)
			GetRowData(l_to)[i] |= src_relation.GetRowData(l_from)[i];
	}
	inline bool Get(uint32_t l, uint32_t r) const { return BitsetGet(GetRowData(l), r); }

	inline uint64_t *GetRowData(uint32_t l) { return m_bit_matrix.data() + l * m_size_r; }
	inline const uint64_t *GetRowData(uint32_t l) const { return m_bit_matrix.data() + l * m_size_r; }
	inline uint32_t GetRowSize() const { return m_size_r; }

	inline Relation GetTransposed() const {
		Relation trans;
		trans.Reset(m_count_r, m_count_l);
		for (uint32_t r = 0; r < m_count_r; ++r)
			for (uint32_t l = 0; l < m_count_l; ++l)
				if (Get(l, r))
					trans.Set(r, l);
		return trans;
	}
};

#endif // MYVK_RG_EXE_RELATION_HPP
