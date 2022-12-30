#ifndef MYVK_RG_PASS_HPP
#define MYVK_RG_PASS_HPP

#include "Input.hpp"
#include "PassBase.hpp"
#include "Resource.hpp"

namespace myvk_rg {

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
