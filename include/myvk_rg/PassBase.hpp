#ifndef MYVK_RG_PASS_BASE_HPP
#define MYVK_RG_PASS_BASE_HPP

#include "Input.hpp"

#include "myvk/CommandBuffer.hpp"

namespace myvk_rg {

class PassBase : public ObjectBase {
private:
	// PassGroup
	const std::vector<PassBase *> *m_p_pass_pool_sequence{};

	// Pass
	const _details_rg_pool_::InputPoolData *m_p_input_pool_data{};
	const DescriptorSetData *m_p_descriptor_set_data{};
	const AttachmentData *m_p_attachment_data{};
	// const _details_rg_pool_::ResourcePoolData *m_p_resource_pool_data{};

	mutable struct {
		uint32_t index;
		bool visited;
		// uint32_t in_degree;
	} m_traversal_data{};

	template <typename, uint8_t, bool> friend class Pass;
	template <typename, bool> friend class PassGroup;
	template <typename, bool> friend class GraphicsPass;
	friend class RenderGraphBase;

public:
	inline PassBase() = default;
	inline PassBase(PassBase &&) noexcept = default;
	inline ~PassBase() override = default;

	inline bool IsPassGroup() const { return m_p_pass_pool_sequence; }

	virtual void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) = 0;
};

template <typename Derived> class PassPool : public Pool<Derived, PassBase> {
private:
	using _PassPool = Pool<Derived, PassBase>;
	std::vector<PassBase *> m_pass_sequence;

public:
	inline PassPool() = default;
	inline PassPool(PassPool &&) noexcept = default;
	inline ~PassPool() override = default;

protected:
	template <typename PassType, typename... Args, typename = std::enable_if_t<std::is_base_of_v<PassBase, PassType>>>
	inline PassType *PushPass(const PoolKey &pass_key, Args &&...args) {
		PassType *ret =
			_PassPool::template CreateAndInitialize<0, PassType, Args...>(pass_key, std::forward<Args>(args)...);
		assert(ret);
		m_pass_sequence.push_back(ret);
		return ret;
	}
	// inline void DeletePass(const PoolKey &pass_key) { return PassPool::Delete(pass_key); }

	const std::vector<PassBase *> &GetPassSequence() const { return m_pass_sequence; }

	template <typename PassType = PassBase,
		typename = std::enable_if_t<std::is_base_of_v<PassBase, PassType> || std::is_same_v<PassBase, PassType>>>
	inline PassType *GetPass(const PoolKey &pass_key) const {
		return _PassPool::template Get<0, PassType>(pass_key);
	}
	inline void ClearPasses() {
		m_pass_sequence.clear();
		_PassPool::Clear();
	}
};

} // namespace myvk_rg

#endif
