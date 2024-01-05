#ifndef MYVK_RG_RESOURCE_IO_HPP
#define MYVK_RG_RESOURCE_IO_HPP

#include <optional>

#include "Alias.hpp"
#include "Object.hpp"
#include "Usage.hpp"

#include "myvk/DescriptorSet.hpp"
#include "myvk/Sampler.hpp"

namespace myvk_rg::interface {

// Resource Input
class InputBase : public ObjectBase {
private:
	AliasBase m_resource{};
	Usage m_usage{};
	VkPipelineStageFlags2 m_pipeline_stages{};
	std::optional<uint32_t> m_opt_descriptor_binding;

public:
	inline InputBase(Parent parent, const AliasBase &resource, Usage usage, VkPipelineStageFlags2 pipeline_stages,
	                 std::optional<uint32_t> opt_descriptor_binding = std::nullopt,
	                 std::optional<uint32_t> = std::nullopt)
	    : ObjectBase(parent), m_resource{resource}, m_usage{usage}, m_pipeline_stages{pipeline_stages},
	      m_opt_descriptor_binding{opt_descriptor_binding} {}
	inline virtual ~InputBase() = default;
	inline Usage GetUsage() const { return m_usage; }
	inline VkPipelineStageFlags2 GetPipelineStages() const { return m_pipeline_stages; }
	inline const auto &GetOptDescriptorBinding() const { return m_opt_descriptor_binding; }

	inline const AliasBase &GetResource() const { return m_resource; }
	inline ResourceType GetType() const { return m_resource.GetType(); }
};

class ImageInput final : public InputBase {
private:
	std::optional<uint32_t> m_opt_attachment_index;

public:
	inline ImageInput(Parent parent, const ImageAliasBase &resource, Usage usage, VkPipelineStageFlags2 pipeline_stages,
	                  std::optional<uint32_t> opt_descriptor_binding = std::nullopt,
	                  std::optional<uint32_t> opt_attachment_index = std::nullopt)
	    : InputBase(parent, resource, usage, pipeline_stages, opt_descriptor_binding),
	      m_opt_attachment_index{opt_attachment_index} {}
	inline ~ImageInput() final = default;

	inline const ImageAliasBase &GetResource() const {
		return static_cast<const ImageAliasBase &>(InputBase::GetResource());
	}
	inline static ResourceType GetType() { return ResourceType::kImage; }
	inline const auto &GetOptAttachmentIndex() const { return m_opt_attachment_index; }
	inline auto GetOutput() const { return OutputImageAlias(this); }
};

class BufferInput final : public InputBase {
public:
	inline BufferInput(Parent parent, const BufferAliasBase &resource, Usage usage,
	                   VkPipelineStageFlags2 pipeline_stages,
	                   std::optional<uint32_t> opt_descriptor_binding = std::nullopt,
	                   std::optional<uint32_t> = std::nullopt)
	    : InputBase(parent, resource, usage, pipeline_stages, opt_descriptor_binding) {}
	inline ~BufferInput() final = default;

	inline const BufferAliasBase &GetResource() const {
		return static_cast<const BufferAliasBase &>(InputBase::GetResource());
	}
	inline static ResourceType GetType() { return ResourceType::kBuffer; }
	inline auto GetOutput() const { return OutputBufferAlias(this); }
};

} // namespace myvk_rg::interface

#endif
