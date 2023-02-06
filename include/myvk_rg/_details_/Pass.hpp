#ifndef MYVK_RG_PASS_HPP
#define MYVK_RG_PASS_HPP

#include "Input.hpp"
#include "RenderGraphBase.hpp"
#include "Resource.hpp"

#include <myvk/CommandBuffer.hpp>

namespace myvk_rg::_details_ {

class PassBase : public ObjectBase {
private:
	// PassGroup
	const std::vector<PassBase *> *m_p_pass_pool_sequence{};

	// Pass
	const _details_rg_pool_::InputPoolData *m_p_input_pool_data{};
	const DescriptorSetData *m_p_descriptor_set_data{};
	const AttachmentData *m_p_attachment_data{};

	mutable struct {
	private:
		uint32_t pass_order{};
		friend class RenderGraphResolver;
	} m_resolved_info{};

	mutable struct {
	private:
		uint32_t pass_id{}, subpass_id{};
		friend class RenderGraphScheduler;
	} m_scheduled_info{};

	template <typename, uint8_t> friend class Pass;
	template <typename> friend class PassGroup;
	template <typename> friend class GraphicsPass;
	friend class RenderGraphBase;
	friend class RenderGraphResolver;
	friend class RenderGraphScheduler;
	friend class RenderGraphExecutor;

	template <typename Func> inline void for_each_input(Func &&func) {
		for (auto it = m_p_input_pool_data->pool.begin(); it != m_p_input_pool_data->pool.end(); ++it)
			func(*(m_p_input_pool_data->ValueGet<0, Input>(it)));
	}
	template <typename Func> inline void for_each_input(Func &&func) const {
		for (auto it = m_p_input_pool_data->pool.begin(); it != m_p_input_pool_data->pool.end(); ++it)
			func(m_p_input_pool_data->ValueGet<0, Input>(it));
	}

public:
	inline PassBase() = default;
	inline PassBase(PassBase &&) noexcept = default;
	inline ~PassBase() override = default;

	inline bool IsPassGroup() const { return m_p_pass_pool_sequence; }

	virtual void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const = 0;
};

template <typename Derived> class PassPool : public Pool<Derived, PassBase> {
private:
	using _PassPool = Pool<Derived, PassBase>;

	inline RenderGraphBase *get_render_graph_ptr() {
		static_assert(std::is_base_of_v<ObjectBase, Derived> || std::is_base_of_v<RenderGraphBase, Derived>);
		if constexpr (std::is_base_of_v<ObjectBase, Derived>)
			return static_cast<ObjectBase *>(static_cast<Derived *>(this))->GetRenderGraphPtr();
		else
			return static_cast<RenderGraphBase *>(static_cast<Derived *>(this));
	}

public:
	inline PassPool() = default;
	inline PassPool(PassPool &&) noexcept = default;
	inline ~PassPool() override = default;

protected:
	template <typename PassType, typename... Args, typename = std::enable_if_t<std::is_base_of_v<PassBase, PassType>>>
	inline PassType *CreatePass(const PoolKey &pass_key, Args &&...args) {
		PassType *ret =
		    _PassPool::template CreateAndInitialize<0, PassType, Args...>(pass_key, std::forward<Args>(args)...);
		assert(ret);
		get_render_graph_ptr()->SetCompilePhrases(CompilePhrase::kResolve);
		return ret;
	}
	inline void DeletePass(const PoolKey &pass_key) { return PassPool::Delete(pass_key); }

	template <typename PassType = PassBase,
	          typename = std::enable_if_t<std::is_base_of_v<PassBase, PassType> || std::is_same_v<PassBase, PassType>>>
	inline PassType *GetPass(const PoolKey &pass_key) const {
		return _PassPool::template Get<0, PassType>(pass_key);
	}
	inline void ClearPasses() {
		_PassPool::Clear();
		get_render_graph_ptr()->SetCompilePhrases(CompilePhrase::kResolve);
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

template <typename Derived, uint8_t Flags>
class Pass : public PassBase,
             public InputPool<Derived>,
             public ResourcePool<Derived>,
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

template <typename Derived>
class PassGroup : public PassBase,
                  public ResourcePool<Derived>,
                  public PassPool<Derived>,
                  public AliasOutputPool<Derived> {
public:
	void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &) const final {}

	inline PassGroup() = default;
	inline PassGroup(PassGroup &&) noexcept = default;
	inline ~PassGroup() override = default;
};

} // namespace myvk_rg::_details_

#endif
