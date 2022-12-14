#ifndef MYVK_RG_RESOURCE_IO_HPP
#define MYVK_RG_RESOURCE_IO_HPP

#include "Pool.hpp"
#include "RenderGraphBase.hpp"
#include "Resource.hpp"
#include "Usage.hpp"

#include "myvk/DescriptorSet.hpp"
#include "myvk/Sampler.hpp"

namespace myvk_rg::_details_ {

// Alias Output Pool (for Sub-pass)
template <typename Derived>
class AliasOutputPool : public Pool<Derived, BufferAlias>, public Pool<Derived, ImageAlias> {
private:
	template <typename Type, typename AliasType>
	inline Type *make_alias_output(const PoolKey &output_key, Type *resource) {
		using Pool = Pool<Derived, AliasType>;
		AliasType *alias = Pool ::template Get<0, AliasType>(output_key);
		if (alias == nullptr) {
			alias = Pool ::template CreateAndInitialize<0, AliasType>(output_key, resource);
		} else {
			if (alias->GetPointedResource() == resource)
				return alias;
			alias = Pool ::template Initialize<0, AliasType>(output_key, resource);
		}
		// Redirect ProducerPassPtr
		static_cast<ResourceBase *>(alias)->set_producer_pass_ptr(resource->GetProducerPassPtr());
		return alias;
	}

public:
	inline AliasOutputPool() = default;
	inline AliasOutputPool(AliasOutputPool &&) noexcept = default;
	inline virtual ~AliasOutputPool() = default;

protected:
	inline BufferBase *MakeBufferAliasOutput(const PoolKey &buffer_alias_output_key, BufferBase *buffer_output) {
		return make_alias_output<BufferBase, BufferAlias>(buffer_alias_output_key, buffer_output);
	}
	inline ImageBase *MakeImageAliasOutput(const PoolKey &image_alias_output_key, ImageBase *image_output) {
		return make_alias_output<ImageBase, ImageAlias>(image_alias_output_key, image_output);
	}
	// TODO: MakeCombinedImageOutput
	inline void RemoveBufferAliasOutput(const PoolKey &buffer_alias_output_key) {
		Pool<Derived, BufferAlias>::Delete(buffer_alias_output_key);
	}
	inline void RemoveImageAliasOutput(const PoolKey &image_alias_output_key) {
		Pool<Derived, ImageAlias>::Delete(image_alias_output_key);
	}
	inline void ClearBufferAliasOutputs() { Pool<Derived, BufferAlias>::Clear(); }
	inline void ClearImageAliasOutputs() { Pool<Derived, ImageAlias>::Clear(); }
};

// Resource Input
class Input {
private:
	std::variant<ImageBase *, BufferBase *> m_resource_ptr{};
	Usage m_usage{};
	VkPipelineStageFlags2 m_usage_pipeline_stages{};
	uint32_t m_descriptor_binding{UINT32_MAX};
	uint32_t m_attachment_index{UINT32_MAX};

public:
	inline Input() = default;
	template <typename Type>
	Input(Type *resource_ptr, Usage usage, VkPipelineStageFlags2 usage_pipeline_stages,
	      uint32_t descriptor_binding = UINT32_MAX, uint32_t attachment_index = UINT32_MAX)
	    : m_resource_ptr{resource_ptr}, m_usage{usage}, m_usage_pipeline_stages{usage_pipeline_stages},
	      m_descriptor_binding{descriptor_binding}, m_attachment_index{attachment_index} {}
	template <typename Type = ResourceBase> inline Type *GetResource() const {
		return std::visit(
		    [](auto arg) -> Type * {
			    using CURType = std::decay_t<decltype(*arg)>;
			    if constexpr (std::is_same_v<Type, CURType> || std::is_base_of_v<Type, CURType>)
				    return arg;
			    return nullptr;
		    },
		    m_resource_ptr);
	}
	inline Usage GetUsage() const { return m_usage; }
	inline VkPipelineStageFlags2 GetUsagePipelineStages() const { return m_usage_pipeline_stages; }
	inline uint32_t GetDescriptorBinding() const { return m_descriptor_binding; }
	inline uint32_t GetAttachmentIndex() const { return m_attachment_index; }
};

// Input Pool
namespace _details_rg_pool_ {
using InputPoolData = PoolData<Input, PoolVariant<BufferAlias, ImageAlias>>;
}
template <typename Derived> class InputPool : public Pool<Derived, Input, PoolVariant<BufferAlias, ImageAlias>> {
private:
	using _InputPool = Pool<Derived, Input, PoolVariant<BufferAlias, ImageAlias>>;

	inline RenderGraphBase *get_render_graph_ptr() {
		static_assert(std::is_base_of_v<ObjectBase, Derived> || std::is_base_of_v<RenderGraphBase, Derived>);
		if constexpr (std::is_base_of_v<ObjectBase, Derived>)
			return static_cast<ObjectBase *>(static_cast<Derived *>(this))->GetRenderGraphPtr();
		else
			return static_cast<RenderGraphBase *>(static_cast<Derived *>(this));
	}

	template <typename... Args> inline Input *add_input(const PoolKey &input_key, Args &&...input_args) {
		auto ret = _InputPool::template CreateAndInitialize<0, Input>(input_key, std::forward<Args>(input_args)...);
		assert(ret);
		get_render_graph_ptr()->m_compile_phrase.generate_pass_sequence = true;
		return ret;
	}

	template <typename Type, typename AliasType> inline Type *make_output(const PoolKey &input_key) {
		const Input *input = _InputPool::template Get<0, Input>(input_key);
		assert(input && !UsageIsReadOnly(input->GetUsage()));
		if (!input || UsageIsReadOnly(input->GetUsage())) // Read-Only input should not produce an output
			return nullptr;
		Type *resource = input->GetResource<Type>();
		assert(resource);
		if (!resource)
			return nullptr;
		else if (resource->GetProducerPassPtr() == (PassBase *)static_cast<Derived *>(this))
			return resource;
		else
			return _InputPool::template InitializeOrGet<1, AliasType>(input_key, resource);
	}

	template <typename> friend class DescriptorInputSlot;
	template <typename> friend class AttachmentInputSlot;

public:
	inline InputPool() { static_assert(std::is_base_of_v<PassBase, Derived>); }
	inline InputPool(InputPool &&) noexcept = default;
	inline ~InputPool() override = default;

protected:
	template <Usage Usage,
	          typename = std::enable_if_t<!kUsageIsAttachment<Usage> && !kUsageIsDescriptor<Usage> &&
	                                      kUsageHasSpecifiedPipelineStages<Usage> && kUsageForBuffer<Usage>>>
	inline bool AddInput(const PoolKey &input_key, BufferBase *buffer) {
		return add_input(input_key, buffer, Usage, kUsageGetSpecifiedPipelineStages<Usage>);
	}
	template <
	    Usage Usage, VkPipelineStageFlags2 PipelineStageFlags,
	    typename = std::enable_if_t<
	        !kUsageIsAttachment<Usage> && !kUsageIsDescriptor<Usage> && !kUsageHasSpecifiedPipelineStages<Usage> &&
	        (PipelineStageFlags & kUsageGetOptionalPipelineStages<Usage>) == PipelineStageFlags &&
	        kUsageForBuffer<Usage>>>
	inline bool AddInput(const PoolKey &input_key, BufferBase *buffer) {
		return add_input(input_key, buffer, Usage, PipelineStageFlags);
	}
	template <Usage Usage,
	          typename = std::enable_if_t<!kUsageIsAttachment<Usage> && !kUsageIsDescriptor<Usage> &&
	                                      kUsageHasSpecifiedPipelineStages<Usage> && kUsageForImage<Usage>>>
	inline bool AddInput(const PoolKey &input_key, ImageBase *image) {
		return add_input(input_key, image, Usage, kUsageGetSpecifiedPipelineStages<Usage>);
	}
	template <
	    Usage Usage, VkPipelineStageFlags2 PipelineStageFlags,
	    typename = std::enable_if_t<
	        !kUsageIsAttachment<Usage> && !kUsageIsDescriptor<Usage> && !kUsageHasSpecifiedPipelineStages<Usage> &&
	        (PipelineStageFlags & kUsageGetOptionalPipelineStages<Usage>) == PipelineStageFlags &&
	        kUsageForImage<Usage>>>
	inline bool AddInput(const PoolKey &input_key, ImageBase *image) {
		return add_input(input_key, image, Usage, PipelineStageFlags);
	}
	// TODO: AddCombinedImageInput()
	inline const Input *GetInput(const PoolKey &input_key) const {
		return _InputPool::template Get<0, Input>(input_key);
	}
	inline BufferBase *MakeBufferOutput(const PoolKey &input_buffer_key) {
		return make_output<BufferBase, BufferAlias>(input_buffer_key);
	}
	inline ImageBase *MakeImageOutput(const PoolKey &input_image_key) {
		return make_output<ImageBase, ImageAlias>(input_image_key);
	}
	inline void RemoveInput(const PoolKey &input_key);
	inline void ClearInputs();
};

class DescriptorBinding {
private:
	const Input *m_p_input{};
	myvk::Ptr<myvk::Sampler> m_sampler{};

public:
	inline DescriptorBinding() = default;
	inline explicit DescriptorBinding(const Input *input, const myvk::Ptr<myvk::Sampler> &sampler = nullptr)
	    : m_p_input{input}, m_sampler{sampler} {}
	inline void SetInput(const Input *input) { m_p_input = input; }
	inline void SetSampler(const myvk::Ptr<myvk::Sampler> &sampler) { m_sampler = sampler; }
	inline void Reset() {
		m_sampler.reset();
		m_p_input = nullptr;
	}
	inline const Input *GetInputPtr() const { return m_p_input; }
	inline const myvk::Ptr<myvk::Sampler> &GetVkSampler() const { return m_sampler; }
};

class DescriptorSetData {
private:
	std::unordered_map<uint32_t, DescriptorBinding> m_bindings;

	mutable myvk::Ptr<myvk::DescriptorSetLayout> m_descriptor_set_layout;
	mutable std::vector<myvk::Ptr<myvk::DescriptorSet>> m_descriptor_sets;

	mutable bool m_modified = true;

	template <typename> friend class DescriptorInputSlot;

public:
	inline bool IsBindingExist(uint32_t binding) const { return m_bindings.find(binding) != m_bindings.end(); }
	inline void AddBinding(uint32_t binding, const Input *input, const myvk::Ptr<myvk::Sampler> &sampler = nullptr) {
		m_bindings.insert({binding, DescriptorBinding{input, sampler}});
		m_modified = true;
	}
	inline void RemoveBinding(uint32_t binding) {
		m_bindings.erase(binding);
		m_modified = true;
	}
	inline void ClearBindings() {
		m_bindings.clear();
		m_modified = true;
	}

	const myvk::Ptr<myvk::DescriptorSetLayout> &GetVkDescriptorSetLayout(const myvk::Ptr<myvk::Device> &device) const;
};

class AttachmentData {
private:
	std::vector<const Input *> m_color_attachments, m_input_attachments;
	const Input *m_depth_attachment{};

public:
	inline AttachmentData() {
		m_color_attachments.reserve(8);
		m_input_attachments.reserve(8);
	}
	inline bool IsColorAttachmentExist(uint32_t index) const {
		return index < m_color_attachments.size() && m_color_attachments[index];
	}
	inline bool IsInputAttachmentExist(uint32_t index) const {
		return index < m_input_attachments.size() && m_input_attachments[index];
	}
	inline bool IsDepthAttachmentExist() const { return m_depth_attachment; }
	inline void AddColorAttachment(uint32_t index, const Input *input) {
		if (m_color_attachments.size() <= index)
			m_color_attachments.resize(index + 1);
		m_color_attachments[index] = input;
	}
	inline void AddInputAttachment(uint32_t index, const Input *input) {
		if (m_input_attachments.size() <= index)
			m_input_attachments.resize(index + 1);
		m_input_attachments[index] = input;
	}
	inline void SetDepthAttachment(const Input *input) { m_depth_attachment = input; }
	inline void RemoveColorAttachment(uint32_t index) {
		m_color_attachments[index] = nullptr;
		while (!m_color_attachments.empty() && m_color_attachments.back() == nullptr)
			m_color_attachments.pop_back();
	}
	inline void RemoveInputAttachment(uint32_t index) {
		m_input_attachments[index] = nullptr;
		while (!m_input_attachments.empty() && m_input_attachments.back() == nullptr)
			m_input_attachments.pop_back();
	}
	inline void ResetDepthAttachment() { m_depth_attachment = nullptr; }
	inline void ClearAttachmens() {
		m_color_attachments.clear();
		m_input_attachments.clear();
		m_depth_attachment = nullptr;
	}
};

template <typename Derived> class DescriptorInputSlot {
private:
	DescriptorSetData m_descriptor_set_data;

	inline InputPool<Derived> *get_input_pool_ptr() {
		static_assert(std::is_base_of_v<InputPool<Derived>, Derived>);
		return (InputPool<Derived> *)static_cast<Derived *>(this);
	}
	inline const InputPool<Derived> *get_input_pool_ptr() const {
		static_assert(std::is_base_of_v<InputPool<Derived>, Derived>);
		return (const InputPool<Derived> *)static_cast<const Derived *>(this);
	}

	inline const RenderGraphBase *get_render_graph_ptr() const {
		static_assert(std::is_base_of_v<ObjectBase, Derived> || std::is_base_of_v<RenderGraphBase, Derived>);
		if constexpr (std::is_base_of_v<ObjectBase, Derived>)
			return static_cast<const ObjectBase *>(static_cast<const Derived *>(this))->GetRenderGraphPtr();
		else
			return static_cast<const RenderGraphBase *>(static_cast<const Derived *>(this));
	}

	template <typename Type>
	inline Input *add_input_descriptor(const PoolKey &input_key, Type *resource, Usage usage,
	                                   VkPipelineStageFlags2 pipeline_stage_flags, uint32_t binding,
	                                   const myvk::Ptr<myvk::Sampler> &sampler = nullptr,
	                                   uint32_t attachment_index = UINT32_MAX) {
		assert(!m_descriptor_set_data.IsBindingExist(binding));
		if (m_descriptor_set_data.IsBindingExist(binding))
			return nullptr;
		auto input = get_input_pool_ptr()->add_input(input_key, resource, usage, pipeline_stage_flags, binding,
		                                             attachment_index);
		assert(input);
		if (!input)
			return nullptr;
		m_descriptor_set_data.AddBinding(binding, input, sampler);
		return input;
	}

	inline void pre_remove_input(const Input *input) {
		if (UsageIsDescriptor(input->GetUsage())) {
			assert(~(input->GetDescriptorBinding()));
			m_descriptor_set_data.RemoveBinding(input->GetDescriptorBinding());
		}
	}
	inline void pre_clear_inputs() { m_descriptor_set_data.ClearBindings(); }

	template <typename> friend class InputPool;
	template <typename> friend class AttachmentInputSlot;

public:
	inline DescriptorInputSlot() = default;
	inline DescriptorInputSlot(DescriptorInputSlot &&) noexcept = default;
	inline ~DescriptorInputSlot() = default;

protected:
	inline const DescriptorSetData &GetDescriptorSetData() const { return m_descriptor_set_data; }

	template <uint32_t Binding, Usage Usage,
	          typename = std::enable_if_t<!kUsageIsAttachment<Usage> && kUsageIsDescriptor<Usage> &&
	                                      kUsageHasSpecifiedPipelineStages<Usage> && kUsageForBuffer<Usage>>>
	inline bool AddDescriptorInput(const PoolKey &input_key, BufferBase *buffer) {
		return add_input_descriptor(input_key, buffer, Usage, kUsageGetSpecifiedPipelineStages<Usage>, Binding);
	}
	template <uint32_t Binding, Usage Usage, VkPipelineStageFlags2 PipelineStageFlags,
	          typename = std::enable_if_t<
	              !kUsageIsAttachment<Usage> && kUsageIsDescriptor<Usage> && !kUsageHasSpecifiedPipelineStages<Usage> &&
	              (PipelineStageFlags & kUsageGetOptionalPipelineStages<Usage>) == PipelineStageFlags &&
	              kUsageForBuffer<Usage>>>
	inline bool AddDescriptorInput(const PoolKey &input_key, BufferBase *buffer) {
		return add_input_descriptor(input_key, buffer, Usage, PipelineStageFlags, Binding);
	}
	template <uint32_t Binding, Usage Usage,
	          typename = std::enable_if_t<!kUsageIsAttachment<Usage> && Usage != Usage::kSampledImage &&
	                                      kUsageIsDescriptor<Usage> && kUsageHasSpecifiedPipelineStages<Usage> &&
	                                      kUsageForImage<Usage>>>
	inline bool AddDescriptorInput(const PoolKey &input_key, ImageBase *image) {
		return add_input_descriptor(input_key, image, Usage, kUsageGetSpecifiedPipelineStages<Usage>, Binding);
	}
	template <uint32_t Binding, Usage Usage, VkPipelineStageFlags2 PipelineStageFlags,
	          typename = std::enable_if_t<!kUsageIsAttachment<Usage> && Usage != Usage::kSampledImage &&
	                                      kUsageIsDescriptor<Usage> && !kUsageHasSpecifiedPipelineStages<Usage> &&
	                                      (PipelineStageFlags & kUsageGetOptionalPipelineStages<Usage>) ==
	                                          PipelineStageFlags &&
	                                      kUsageForImage<Usage>>>
	inline bool AddDescriptorInput(const PoolKey &input_key, ImageBase *image) {
		return add_input_descriptor(input_key, image, Usage, PipelineStageFlags, Binding);
	}
	template <uint32_t Binding, Usage Usage,
	          typename = std::enable_if_t<Usage == Usage::kSampledImage && kUsageHasSpecifiedPipelineStages<Usage> &&
	                                      kUsageForImage<Usage>>>
	inline bool AddDescriptorInput(const PoolKey &input_key, ImageBase *image,
	                               const myvk::Ptr<myvk::Sampler> &sampler) {
		return add_input_descriptor(input_key, image, Usage, kUsageGetSpecifiedPipelineStages<Usage>, Binding, sampler);
	}
	template <uint32_t Binding, Usage Usage, VkPipelineStageFlags2 PipelineStageFlags,
	          typename = std::enable_if_t<Usage == Usage::kSampledImage && !kUsageHasSpecifiedPipelineStages<Usage> &&
	                                      (PipelineStageFlags & kUsageGetOptionalPipelineStages<Usage>) ==
	                                          PipelineStageFlags &&
	                                      kUsageForImage<Usage>>>
	inline bool AddDescriptorInput(const PoolKey &input_key, ImageBase *image,
	                               const myvk::Ptr<myvk::Sampler> &sampler) {
		return add_input_descriptor(input_key, image, Usage, PipelineStageFlags, Binding, sampler);
	}
	inline const myvk::Ptr<myvk::DescriptorSetLayout> &GetDescriptorSetLayout() const {
		return m_descriptor_set_data.GetVkDescriptorSetLayout(get_render_graph_ptr()->GetDevicePtr());
	}
};

template <typename Derived> class AttachmentInputSlot {
private:
	AttachmentData m_attachment_data;

	inline InputPool<Derived> *get_input_pool_ptr() {
		static_assert(std::is_base_of_v<InputPool<Derived>, Derived>);
		return (InputPool<Derived> *)static_cast<Derived *>(this);
	}
	inline const InputPool<Derived> *get_input_pool_ptr() const {
		static_assert(std::is_base_of_v<InputPool<Derived>, Derived>);
		return (const InputPool<Derived> *)static_cast<const Derived *>(this);
	}

	inline DescriptorInputSlot<Derived> *get_descriptor_slot_ptr() {
		static_assert(std::is_base_of_v<DescriptorInputSlot<Derived>, Derived>);
		return (DescriptorInputSlot<Derived> *)static_cast<Derived *>(this);
	}
	inline const DescriptorInputSlot<Derived> *get_descriptor_slot_ptr() const {
		static_assert(std::is_base_of_v<DescriptorInputSlot<Derived>, Derived>);
		return (const DescriptorInputSlot<Derived> *)static_cast<const Derived *>(this);
	}

	inline void pre_remove_input(const Input *input) {
		if (UsageIsDepthAttachment(input->GetUsage()))
			m_attachment_data.ResetDepthAttachment();
		else if (UsageIsColorAttachment(input->GetUsage())) {
			assert(~(input->GetAttachmentIndex()));
			m_attachment_data.RemoveColorAttachment(input->GetAttachmentIndex());
		} else if (input->GetUsage() == Usage::kInputAttachment) {
			assert(~(input->GetAttachmentIndex()));
			m_attachment_data.RemoveInputAttachment(input->GetAttachmentIndex());
		}
	}
	inline void pre_clear_inputs() { m_attachment_data.ClearAttachmens(); }

	template <typename> friend class InputPool;

public:
	inline AttachmentInputSlot() {
		// TODO: Should AttachmentData be Object ?
		/* static_assert(std::is_base_of_v<ObjectBase, Derived> || std::is_base_of_v<RenderGraphBase, Derived>);
		if constexpr (std::is_base_of_v<ObjectBase, Derived>)
		    m_descriptor_set_data.set_render_graph_ptr(
		        ((ObjectBase *)static_cast<Derived *>(this))->GetRenderGraphPtr());
		else
		    m_descriptor_set_data.set_render_graph_ptr((RenderGraphBase *)static_cast<Derived *>(this)); */
	}
	inline AttachmentInputSlot(AttachmentInputSlot &&) noexcept = default;
	inline ~AttachmentInputSlot() = default;

protected:
	inline const AttachmentData &GetAttachmentData() const { return m_attachment_data; }

	template <uint32_t Index, Usage Usage, typename = std::enable_if_t<kUsageIsColorAttachment<Usage>>>
	inline bool AddColorAttachmentInput(const PoolKey &input_key, ImageBase *image) {
		static_assert(kUsageHasSpecifiedPipelineStages<Usage>);

		assert(!m_attachment_data.IsColorAttachmentExist(Index));
		if (m_attachment_data.IsColorAttachmentExist(Index))
			return false;
		auto input = get_input_pool_ptr()->add_input(input_key, image, Usage, kUsageGetSpecifiedPipelineStages<Usage>,
		                                             UINT32_MAX, Index);
		assert(input);
		if (!input)
			return false;
		m_attachment_data.AddColorAttachment(Index, input);
		return true;
	}

	template <uint32_t AttachmentIndex, uint32_t DescriptorBinding>
	inline bool AddInputAttachmentInput(const PoolKey &input_key, ImageBase *image) {
		static_assert(kUsageHasSpecifiedPipelineStages<Usage::kInputAttachment>);

		assert(!m_attachment_data.IsInputAttachmentExist(AttachmentIndex));
		if (m_attachment_data.IsInputAttachmentExist(AttachmentIndex))
			return false;
		auto input = get_descriptor_slot_ptr()->add_input_descriptor(
		    input_key, image, Usage::kInputAttachment, kUsageGetSpecifiedPipelineStages<Usage::kInputAttachment>,
		    DescriptorBinding, nullptr, AttachmentIndex);
		assert(input);
		if (!input)
			return false;
		m_attachment_data.AddInputAttachment(AttachmentIndex, input);
		return true;
	}

	template <Usage Usage, typename = std::enable_if_t<kUsageIsDepthAttachment<Usage>>>
	inline bool SetDepthAttachmentInput(const PoolKey &input_key, ImageBase *image) {
		static_assert(kUsageHasSpecifiedPipelineStages<Usage>);

		assert(!m_attachment_data.IsDepthAttachmentExist());
		if (m_attachment_data.IsDepthAttachmentExist())
			return false;
		auto input = get_input_pool_ptr()->add_input(input_key, image, Usage, kUsageGetSpecifiedPipelineStages<Usage>);
		assert(input);
		if (!input)
			return false;
		m_attachment_data.SetDepthAttachment(input);
		return true;
	}
};

template <typename Derived> void InputPool<Derived>::RemoveInput(const PoolKey &input_key) {
	if constexpr (std::is_base_of_v<DescriptorInputSlot<Derived>, Derived> ||
	              std::is_base_of_v<AttachmentInputSlot<Derived>, Derived>) {
		const Input *input = GetInput(input_key);
		if constexpr (std::is_base_of_v<DescriptorInputSlot<Derived>, Derived>)
			((DescriptorInputSlot<Derived> *)static_cast<Derived *>(this))->pre_remove_input(input);
		if constexpr (std::is_base_of_v<AttachmentInputSlot<Derived>, Derived>)
			((AttachmentInputSlot<Derived> *)static_cast<Derived *>(this))->pre_remove_input(input);
	}
	InputPool::Delete(input_key);
	get_render_graph_ptr()->m_compile_phrase.generate_pass_sequence = true;
}

template <typename Derived> void InputPool<Derived>::ClearInputs() {
	if constexpr (std::is_base_of_v<DescriptorInputSlot<Derived>, Derived>)
		((DescriptorInputSlot<Derived> *)static_cast<Derived *>(this))->pre_clear_inputs();
	if constexpr (std::is_base_of_v<AttachmentInputSlot<Derived>, Derived>)
		((AttachmentInputSlot<Derived> *)static_cast<Derived *>(this))->pre_clear_inputs();
	InputPool::Clear();
	get_render_graph_ptr()->m_compile_phrase.generate_pass_sequence = true;
}

} // namespace myvk_rg::_details_

#endif
