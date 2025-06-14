#ifndef COMPUTE_PIPELINE_HPP
#define COMPUTE_PIPELINE_HPP

#include "PipelineBase.hpp"
#include "ShaderModule.hpp"

namespace myvk {
class ComputePipeline : public PipelineBase {
public:
	static Ptr<ComputePipeline> Create(const Ptr<PipelineLayout> &pipeline_layout,
	                                   const VkComputePipelineCreateInfo &create_info);

	static Ptr<ComputePipeline> Create(const Ptr<PipelineLayout> &pipeline_layout,
	                                   const VkPipelineShaderStageCreateInfo &shader_stage_create_info);

	static Ptr<ComputePipeline> Create(const Ptr<PipelineLayout> &pipeline_layout,
	                                   const Ptr<ShaderModule> &shader_module);

	VkPipelineBindPoint GetBindPoint() const override { return VK_PIPELINE_BIND_POINT_COMPUTE; }

	~ComputePipeline() override = default;
};
} // namespace myvk

#endif
