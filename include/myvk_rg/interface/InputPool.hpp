#pragma once
#ifndef MYVK_RG_INPUTPOOL_HPP
#define MYVK_RG_INPUTPOOL_HPP

#include "Input.hpp"
#include "Pool.hpp"

namespace myvk_rg::interface {

template <typename Derived> class InputPool : public Pool<Derived, std::variant<BufferInput, ImageInput>> {
private:
	using PoolBase = Pool<Derived, std::variant<BufferInput, ImageInput>>;

	template <typename InputType, typename... Args> inline auto add_input(const PoolKey &input_key, Args &&...args) {
		static_cast<const ObjectBase *>(static_cast<const Derived *>(this))->EmitEvent(Event::kInputChanged);
		return PoolBase::template Construct<0, InputType>(input_key, std::forward<Args>(args)...);
	}

	template <typename> friend class DescriptorInputSlot;
	template <typename> friend class AttachmentInputSlot;

public:
	inline InputPool() = default;
	inline InputPool(InputPool &&) noexcept = default;
	inline ~InputPool() override = default;

	inline const auto &GetInputPoolData() const { return PoolBase::GetPoolData(); }

protected:
	template <Usage Usage,
	          typename = std::enable_if_t<!kUsageIsAttachment<Usage> && !kUsageIsDescriptor<Usage> &&
	                                      kUsageHasSpecifiedPipelineStages<Usage> && kUsageForBuffer<Usage>>>
	inline void AddInput(const PoolKey &input_key, const BufferAliasBase &buffer) {
		add_input<BufferInput>(input_key, buffer, Usage, kUsageGetSpecifiedPipelineStages<Usage>);
	}
	template <
	    Usage Usage, VkPipelineStageFlags2 PipelineStageFlags,
	    typename = std::enable_if_t<
	        !kUsageIsAttachment<Usage> && !kUsageIsDescriptor<Usage> && !kUsageHasSpecifiedPipelineStages<Usage> &&
	        (PipelineStageFlags & kUsageGetOptionalPipelineStages<Usage>) == PipelineStageFlags &&
	        kUsageForBuffer<Usage>>>
	inline void AddInput(const PoolKey &input_key, const BufferAliasBase &buffer) {
		add_input<BufferInput>(input_key, buffer, Usage, PipelineStageFlags);
	}
	template <Usage Usage,
	          typename = std::enable_if_t<!kUsageIsAttachment<Usage> && !kUsageIsDescriptor<Usage> &&
	                                      kUsageHasSpecifiedPipelineStages<Usage> && kUsageForImage<Usage>>>
	inline void AddInput(const PoolKey &input_key, const ImageAliasBase &image) {
		add_input<ImageInput>(input_key, image, Usage, kUsageGetSpecifiedPipelineStages<Usage>);
	}
	template <
	    Usage Usage, VkPipelineStageFlags2 PipelineStageFlags,
	    typename = std::enable_if_t<
	        !kUsageIsAttachment<Usage> && !kUsageIsDescriptor<Usage> && !kUsageHasSpecifiedPipelineStages<Usage> &&
	        (PipelineStageFlags & kUsageGetOptionalPipelineStages<Usage>) == PipelineStageFlags &&
	        kUsageForImage<Usage>>>
	inline void AddInput(const PoolKey &input_key, const ImageAliasBase &image) {
		add_input<ImageInput>(input_key, image, Usage, PipelineStageFlags);
	}

	template <typename InputType = InputBase> inline const InputType *GetInput(const PoolKey &input_key) const {
		return PoolBase::template Get<0, InputType>(input_key);
	}

	inline OutputBufferAlias MakeBufferOutput(const PoolKey &input_key) {
		return PoolBase::template Get<0, BufferInput>(input_key)->GetOutput();
	}
	inline OutputImageAlias MakeImageOutput(const PoolKey &input_key) {
		return PoolBase::template Get<0, ImageInput>(input_key)->GetOutput();
	}
	inline void ClearInputs();
};

class DescriptorBinding {
private:
	const InputBase *m_input{};
	myvk::Ptr<myvk::Sampler> m_sampler{};

public:
	inline DescriptorBinding() = default;
	inline explicit DescriptorBinding(const InputBase *input, const myvk::Ptr<myvk::Sampler> &sampler = nullptr)
	    : m_input{input}, m_sampler{sampler} {}
	inline auto GetInput() const { return m_input; }
	inline const myvk::Ptr<myvk::Sampler> &GetVkSampler() const { return m_sampler; }
};

class DescriptorSetData {
private:
	std::unordered_map<uint32_t, std::vector<DescriptorBinding>> m_bindings;

	template <typename> friend class DescriptorInputSlot;

public:
	inline bool IsBindingExist(uint32_t binding) const { return m_bindings.find(binding) != m_bindings.end(); }
	inline void AddBinding(uint32_t binding, std::vector<DescriptorBinding> &&inputs) {
		m_bindings.insert({binding, std::move(inputs)});
	}
	inline void ClearBindings() { m_bindings.clear(); }
	inline const auto &GetBindings() const { return m_bindings; }
	// const myvk::Ptr<myvk::DescriptorSetLayout> &GetVkDescriptorSetLayout(const PassBase *pass) const;
	// const myvk::Ptr<myvk::DescriptorSet> &GetVkDescriptorSet(const PassBase *pass) const;
};

class AttachmentData {
private:
	std::vector<const ImageInput *> m_color_attachment_inputs{}, m_input_attachment_inputs{};
	const ImageInput *m_depth_attachment_input{};

public:
	inline AttachmentData() {
		m_color_attachment_inputs.reserve(8);
		m_input_attachment_inputs.reserve(8);
	}
	inline const auto &GetColorAttachmentInputs() const { return m_color_attachment_inputs; }
	inline const auto &GetInputAttachmentInputs() const { return m_input_attachment_inputs; }
	inline auto GetDepthAttachmentInput() const { return m_depth_attachment_input; }
	inline bool IsColorAttachmentExist(uint32_t index) const {
		return index < m_color_attachment_inputs.size() && m_color_attachment_inputs[index];
	}
	inline bool IsInputAttachmentExist(uint32_t index) const {
		return index < m_input_attachment_inputs.size() && m_input_attachment_inputs[index];
	}
	inline bool IsDepthAttachmentExist() const { return m_depth_attachment_input; }
	inline void AddColorAttachment(uint32_t index, const ImageInput *input) {
		if (m_color_attachment_inputs.size() <= index)
			m_color_attachment_inputs.resize(index + 1);
		m_color_attachment_inputs[index] = input;
	}
	inline void AddInputAttachment(uint32_t index, const ImageInput *input) {
		if (m_input_attachment_inputs.size() <= index)
			m_input_attachment_inputs.resize(index + 1);
		m_input_attachment_inputs[index] = input;
	}
	inline void SetDepthAttachment(const ImageInput *input) { m_depth_attachment_input = input; }
	inline void ClearAttachments() {
		m_color_attachment_inputs.clear();
		m_input_attachment_inputs.clear();
		m_depth_attachment_input = nullptr;
	}
};

struct BufferDescriptorInput {
	PoolKey input_key{};
	BufferAliasBase resource{};

private:
	inline static auto get_sampler() { return nullptr; }
	template <typename> friend class DescriptorInputSlot;
};
struct ImageDescriptorInput {
	PoolKey input_key{};
	ImageAliasBase resource{};

private:
	inline static auto get_sampler() { return nullptr; }
	template <typename> friend class DescriptorInputSlot;
};
struct SamplerDescriptorInput {
	PoolKey input_key{};
	ImageAliasBase resource{};
	myvk::Ptr<myvk::Sampler> sampler{};

private:
	inline auto get_sampler() const { return sampler; }
	template <typename> friend class DescriptorInputSlot;
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

	template <typename DescriptorInputType,
	          typename InputType = std::conditional_t<std::is_same_v<DescriptorInputType, BufferDescriptorInput>,
	                                                  BufferInput, ImageInput>>
	inline const InputType *add_input_descriptor(const std::vector<DescriptorInputType> &input_resources, Usage usage,
	                                             VkPipelineStageFlags2 pipeline_stage_flags, uint32_t binding,
	                                             std::optional<uint32_t> opt_attachment_index = std::nullopt) {
		std::vector<DescriptorBinding> bindings;
		bindings.reserve(input_resources.size());
		InputType *input = nullptr;
		for (const auto &p : input_resources) {
			input = get_input_pool_ptr()->template add_input<InputType>(
			    p.input_key, p.resource, usage, pipeline_stage_flags, binding, opt_attachment_index);
			bindings.push_back(DescriptorBinding{input, p.get_sampler()});
		}
		m_descriptor_set_data.AddBinding(binding, std::move(bindings));
		static_cast<const ObjectBase *>(static_cast<const Derived *>(this))->EmitEvent(Event::kDescriptorChanged);
		return input;
	}
	inline void pre_clear_inputs() {
		m_descriptor_set_data.ClearBindings();
		static_cast<const ObjectBase *>(static_cast<const Derived *>(this))->EmitEvent(Event::kDescriptorChanged);
	}

	template <typename> friend class InputPool;
	template <typename> friend class AttachmentInputSlot;

public:
	inline DescriptorInputSlot() = default;
	inline DescriptorInputSlot(DescriptorInputSlot &&) noexcept = default;
	inline ~DescriptorInputSlot() = default;

	inline const DescriptorSetData &GetDescriptorSetData() const { return m_descriptor_set_data; }

protected:
	// Buffer don't specify pipeline stage
	template <uint32_t Binding, Usage Usage,
	          typename = std::enable_if_t<!kUsageIsAttachment<Usage> && kUsageIsDescriptor<Usage> &&
	                                      kUsageHasSpecifiedPipelineStages<Usage> && kUsageForBuffer<Usage>>>
	inline void AddDescriptorInput(const std::vector<BufferDescriptorInput> &buffer_descriptor_array) {
		add_input_descriptor(buffer_descriptor_array, Usage, kUsageGetSpecifiedPipelineStages<Usage>, Binding);
	}
	template <uint32_t Binding, Usage Usage,
	          typename = std::enable_if_t<!kUsageIsAttachment<Usage> && kUsageIsDescriptor<Usage> &&
	                                      kUsageHasSpecifiedPipelineStages<Usage> && kUsageForBuffer<Usage>>>
	inline void AddDescriptorInput(const PoolKey &input_key, const BufferAliasBase &buffer) {
		AddDescriptorInput<Binding, Usage>({BufferDescriptorInput{input_key, buffer}});
	}

	// Buffer specify pipeline stage
	template <uint32_t Binding, Usage Usage, VkPipelineStageFlags2 PipelineStageFlags,
	          typename = std::enable_if_t<
	              !kUsageIsAttachment<Usage> && kUsageIsDescriptor<Usage> && !kUsageHasSpecifiedPipelineStages<Usage> &&
	              (PipelineStageFlags & kUsageGetOptionalPipelineStages<Usage>) == PipelineStageFlags &&
	              kUsageForBuffer<Usage>>>
	inline void AddDescriptorInput(const std::vector<BufferDescriptorInput> &buffer_descriptor_array) {
		add_input_descriptor(buffer_descriptor_array, Usage, PipelineStageFlags, Binding);
	}
	template <uint32_t Binding, Usage Usage, VkPipelineStageFlags2 PipelineStageFlags,
	          typename = std::enable_if_t<
	              !kUsageIsAttachment<Usage> && kUsageIsDescriptor<Usage> && !kUsageHasSpecifiedPipelineStages<Usage> &&
	              (PipelineStageFlags & kUsageGetOptionalPipelineStages<Usage>) == PipelineStageFlags &&
	              kUsageForBuffer<Usage>>>
	inline void AddDescriptorInput(const PoolKey &input_key, const BufferAliasBase &buffer) {
		AddDescriptorInput<Binding, Usage, PipelineStageFlags>({BufferDescriptorInput{input_key, buffer}});
	}

	// Image don't specify pipeline stage
	template <uint32_t Binding, Usage Usage,
	          typename = std::enable_if_t<!kUsageIsAttachment<Usage> && Usage != Usage::kSampledImage &&
	                                      kUsageIsDescriptor<Usage> && kUsageHasSpecifiedPipelineStages<Usage> &&
	                                      kUsageForImage<Usage>>>
	inline void AddDescriptorInput(const std::vector<ImageDescriptorInput> &image_descriptor_array) {
		add_input_descriptor(image_descriptor_array, Usage, kUsageGetSpecifiedPipelineStages<Usage>, Binding);
	}
	template <uint32_t Binding, Usage Usage,
	          typename = std::enable_if_t<!kUsageIsAttachment<Usage> && Usage != Usage::kSampledImage &&
	                                      kUsageIsDescriptor<Usage> && kUsageHasSpecifiedPipelineStages<Usage> &&
	                                      kUsageForImage<Usage>>>
	inline void AddDescriptorInput(const PoolKey &input_key, const ImageAliasBase &image) {
		AddDescriptorInput<Binding, Usage>({ImageDescriptorInput{input_key, image}});
	}

	// Image specify pipeline stage
	template <uint32_t Binding, Usage Usage, VkPipelineStageFlags2 PipelineStageFlags,
	          typename = std::enable_if_t<!kUsageIsAttachment<Usage> && Usage != Usage::kSampledImage &&
	                                      kUsageIsDescriptor<Usage> && !kUsageHasSpecifiedPipelineStages<Usage> &&
	                                      (PipelineStageFlags & kUsageGetOptionalPipelineStages<Usage>) ==
	                                          PipelineStageFlags &&
	                                      kUsageForImage<Usage>>>
	inline void AddDescriptorInput(const std::vector<ImageDescriptorInput> &image_descriptor_array) {
		add_input_descriptor(image_descriptor_array, Usage, PipelineStageFlags, Binding);
	}
	template <uint32_t Binding, Usage Usage, VkPipelineStageFlags2 PipelineStageFlags,
	          typename = std::enable_if_t<!kUsageIsAttachment<Usage> && Usage != Usage::kSampledImage &&
	                                      kUsageIsDescriptor<Usage> && !kUsageHasSpecifiedPipelineStages<Usage> &&
	                                      (PipelineStageFlags & kUsageGetOptionalPipelineStages<Usage>) ==
	                                          PipelineStageFlags &&
	                                      kUsageForImage<Usage>>>
	inline void AddDescriptorInput(const PoolKey &input_key, const ImageAliasBase &image) {
		AddDescriptorInput<Binding, Usage, PipelineStageFlags>({ImageDescriptorInput{input_key, image}});
	}

	// Image + sampler don't specify pipeline stage
	template <uint32_t Binding, Usage Usage,
	          typename = std::enable_if_t<Usage == Usage::kSampledImage && kUsageHasSpecifiedPipelineStages<Usage> &&
	                                      kUsageForImage<Usage>>>
	inline void AddDescriptorInput(const std::vector<SamplerDescriptorInput> &sampler_descriptor_array) {
		add_input_descriptor(sampler_descriptor_array, Usage, kUsageGetSpecifiedPipelineStages<Usage>, Binding);
	}
	template <uint32_t Binding, Usage Usage,
	          typename = std::enable_if_t<Usage == Usage::kSampledImage && kUsageHasSpecifiedPipelineStages<Usage> &&
	                                      kUsageForImage<Usage>>>
	inline void AddDescriptorInput(const PoolKey &input_key, const ImageAliasBase &image,
	                               const myvk::Ptr<myvk::Sampler> &sampler) {
		AddDescriptorInput<Binding, Usage>({SamplerDescriptorInput{input_key, image, sampler}});
	}

	// Image + sampler specify pipeline stage
	template <uint32_t Binding, Usage Usage, VkPipelineStageFlags2 PipelineStageFlags,
	          typename = std::enable_if_t<Usage == Usage::kSampledImage && !kUsageHasSpecifiedPipelineStages<Usage> &&
	                                      (PipelineStageFlags & kUsageGetOptionalPipelineStages<Usage>) ==
	                                          PipelineStageFlags &&
	                                      kUsageForImage<Usage>>>
	inline void AddDescriptorInput(const std::vector<SamplerDescriptorInput> &sampler_descriptor_array) {
		add_input_descriptor(sampler_descriptor_array, Usage, PipelineStageFlags, Binding);
	}
	template <uint32_t Binding, Usage Usage, VkPipelineStageFlags2 PipelineStageFlags,
	          typename = std::enable_if_t<Usage == Usage::kSampledImage && !kUsageHasSpecifiedPipelineStages<Usage> &&
	                                      (PipelineStageFlags & kUsageGetOptionalPipelineStages<Usage>) ==
	                                          PipelineStageFlags &&
	                                      kUsageForImage<Usage>>>
	inline void AddDescriptorInput(const PoolKey &input_key, const ImageAliasBase &image,
	                               const myvk::Ptr<myvk::Sampler> &sampler) {
		AddDescriptorInput<Binding, Usage, PipelineStageFlags>({SamplerDescriptorInput{input_key, image, sampler}});
	}

	/* inline const myvk::Ptr<myvk::DescriptorSetLayout> &GetVkDescriptorSetLayout() const {
	    return m_descriptor_set_data.GetVkDescriptorSetLayout(get_pass_ptr());
	}
	inline const myvk::Ptr<myvk::DescriptorSet> &GetVkDescriptorSet() const {
	    return m_descriptor_set_data.GetVkDescriptorSet(get_pass_ptr());
	} */
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
	inline void pre_clear_inputs() {
		m_attachment_data.ClearAttachments();
		static_cast<const ObjectBase *>(static_cast<const Derived *>(this))->EmitEvent(Event::kAttachmentChanged);
	}

	template <typename> friend class InputPool;

public:
	inline AttachmentInputSlot() = default;
	inline AttachmentInputSlot(AttachmentInputSlot &&) noexcept = default;
	inline ~AttachmentInputSlot() = default;

	inline const AttachmentData &GetAttachmentData() const { return m_attachment_data; }

protected:
	template <uint32_t Index, Usage Usage, typename = std::enable_if_t<kUsageIsColorAttachment<Usage>>>
	inline void AddColorAttachmentInput(const PoolKey &input_key, const ImageAliasBase &image) {
		static_assert(kUsageHasSpecifiedPipelineStages<Usage>);
		static_cast<const ObjectBase *>(static_cast<const Derived *>(this))->EmitEvent(Event::kAttachmentChanged);

		auto input = get_input_pool_ptr()->template add_input<ImageInput>(
		    input_key, image, Usage, kUsageGetSpecifiedPipelineStages<Usage>, std::nullopt, Index);
		m_attachment_data.AddColorAttachment(Index, input);
	}

	template <uint32_t AttachmentIndex, uint32_t DescriptorBinding>
	inline void AddInputAttachmentInput(const PoolKey &input_key, const ImageAliasBase &image) {
		static_assert(kUsageHasSpecifiedPipelineStages<Usage::kInputAttachment>);
		static_cast<const ObjectBase *>(static_cast<const Derived *>(this))->EmitEvent(Event::kAttachmentChanged);

		auto input = get_descriptor_slot_ptr()->add_input_descriptor(
		    std::vector<ImageDescriptorInput>{ImageDescriptorInput{input_key, image}}, Usage::kInputAttachment,
		    kUsageGetSpecifiedPipelineStages<Usage::kInputAttachment>, DescriptorBinding, AttachmentIndex);
		m_attachment_data.AddInputAttachment(AttachmentIndex, input);
	}

	template <Usage Usage, typename = std::enable_if_t<kUsageIsDepthAttachment<Usage>>>
	inline void SetDepthAttachmentInput(const PoolKey &input_key, const ImageAliasBase &image) {
		static_assert(kUsageHasSpecifiedPipelineStages<Usage>);
		static_cast<const ObjectBase *>(static_cast<const Derived *>(this))->EmitEvent(Event::kAttachmentChanged);

		auto input = get_input_pool_ptr()->template add_input<ImageInput>(input_key, image, Usage,
		                                                                  kUsageGetSpecifiedPipelineStages<Usage>);
		m_attachment_data.SetDepthAttachment(input);
	}
};

template <typename Derived> void InputPool<Derived>::ClearInputs() {
	if constexpr (std::is_base_of_v<DescriptorInputSlot<Derived>, Derived>)
		((DescriptorInputSlot<Derived> *)static_cast<Derived *>(this))->pre_clear_inputs();
	if constexpr (std::is_base_of_v<AttachmentInputSlot<Derived>, Derived>)
		((AttachmentInputSlot<Derived> *)static_cast<Derived *>(this))->pre_clear_inputs();
	InputPool::Clear();
	static_cast<const ObjectBase *>(static_cast<const Derived *>(this))->EmitEvent(Event::kInputChanged);
}

} // namespace myvk_rg::interface

#endif // MYVK_INPUTPOOL_HPP
