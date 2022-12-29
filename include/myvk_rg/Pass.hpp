#ifndef MYVK_RG_PASS_HPP
#define MYVK_RG_PASS_HPP

#include "ResourceIO.hpp"
#include "ResourcePool.hpp"

namespace myvk_rg {

namespace _details_rg_pool_ {
using PassPoolData = PoolData<PassBase>;
}
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

namespace _details_rg_pass_ {
struct NoResourcePool {};
struct NoPassPool {};
struct NoAliasOutputPool {};
struct NoDescriptorInputSlot {};
struct NoAttachmentInputSlot {};
} // namespace _details_rg_pass_

struct PassFlag {
	enum : uint8_t { kDescriptor = 4u, kGraphics = 8u, kCompute = 16u };
};

template <typename Derived, uint8_t Flags, bool EnableResource = false>
class Pass : public PassBase,
             public InputPool<Derived>,
             public std::conditional_t<EnableResource, ResourcePool<Derived>, _details_rg_pass_::NoResourcePool>,
             public std::conditional_t<(Flags & PassFlag::kDescriptor) != 0, DescriptorInputSlot<Derived>,
                                       _details_rg_pass_::NoDescriptorInputSlot>,
             public std::conditional_t<(Flags & PassFlag::kGraphics) != 0, AttachmentInputSlot<Derived>,
                                       _details_rg_pass_::NoAttachmentInputSlot> {
public:
	inline Pass() {
		m_p_input_pool_data = &InputPool<Derived>::GetPoolData();
		/* if constexpr ((Flags & PassFlag::kEnableResourceAllocation) != 0)
		    m_p_resource_pool_data = &ResourcePool<Derived>::GetPoolData();
		if constexpr ((Flags & PassFlag::kEnableSubpass) != 0)
		    m_p_pass_pool_data = &PassPool<Derived>::GetPoolData(); */
		if constexpr ((Flags & PassFlag::kDescriptor) != 0)
			m_p_descriptor_set_data = &DescriptorInputSlot<Derived>::GetDescriptorSetData();
		if constexpr ((Flags & PassFlag::kGraphics) != 0)
			m_p_attachment_data = &AttachmentInputSlot<Derived>::GetAttachmentData();
	}
	inline Pass(Pass &&) noexcept = default;
	inline ~Pass() override = default;
};

template <typename Derived, bool EnableResource = false>
class PassGroup : public PassBase,
                  public std::conditional_t<EnableResource, ResourcePool<Derived>, _details_rg_pass_::NoResourcePool>,
                  public PassPool<Derived>,
                  public AliasOutputPool<Derived> {
public:
	void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &) final {}

	inline PassGroup() { m_p_pass_pool_sequence = &PassPool<Derived>::GetPassSequence(); }
	inline PassGroup(PassGroup &&) noexcept = default;
	inline ~PassGroup() override = default;
};
} // namespace myvk_rg

#endif
