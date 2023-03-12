#include <cmath>
#include <iostream>

#include <myvk/FrameManager.hpp>
#include <myvk/GLFWHelper.hpp>
#include <myvk/ImGuiHelper.hpp>
#include <myvk/ImGuiRenderer.hpp>
#include <myvk/Instance.hpp>
#include <myvk/Queue.hpp>
#include <myvk_rg/RenderGraph.hpp>
#include <myvk_rg/pass/ImGuiPass.hpp>
#include <myvk_rg/pass/ImageBlitPass.hpp>
#include <myvk_rg/resource/SwapchainImage.hpp>

constexpr uint32_t kFrameCount = 3;

#if 0
class CullPass final : public myvk_rg::ComputePassBase {
private:
	MYVK_RG_OBJECT_FRIENDS
	inline void Initialize(myvk_rg::ImageInput depth_hierarchy) {
		auto draw_list = CreateResource<myvk_rg::ManagedBuffer>({"draw_list"});
		draw_list->SetSize(1024 * 1024);
		AddDescriptorInput<0, myvk_rg::Usage::kSampledImage, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT>(
		    {"depth_hierarchy"}, depth_hierarchy,
		    myvk::Sampler::Create(GetRenderGraphPtr()->GetDevicePtr(), VK_FILTER_LINEAR,
		                          VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));
		AddDescriptorInput<1, myvk_rg::Usage::kStorageBufferW, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT>({"draw_list"},
		                                                                                               draw_list);
	}

public:
	inline auto GetDrawListOutput() { return MakeBufferOutput({"draw_list"}); }
	inline void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const final {}
	inline myvk::Ptr<myvk::ComputePipeline> CreateComputePipeline() const final { return nullptr; }
};

class DepthHierarchyPass final : public myvk_rg::PassGroupBase {
private:
	class TopSubPass final : public myvk_rg::GraphicsPassBase {
	private:
		MYVK_RG_OBJECT_FRIENDS
		inline void Initialize(myvk_rg::ImageInput depth_img) {
			auto top_level_img = CreateResource<myvk_rg::ManagedImage>({"top"}, VK_FORMAT_R32_SFLOAT);
			top_level_img->SetCanvasSize(0, 1);
			AddColorAttachmentInput<0, myvk_rg::Usage::kColorAttachmentW>({"top"}, top_level_img);
			AddDescriptorInput<0, myvk_rg::Usage::kSampledImage, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT>(
			    {"depth"}, depth_img,
			    myvk::Sampler::Create(GetRenderGraphPtr()->GetDevicePtr(), VK_FILTER_NEAREST,
			                          VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));
		}

	public:
		inline auto GetTopOutput() { return MakeImageOutput({"top"}); }
		inline void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const final {}
		inline myvk::Ptr<myvk::GraphicsPipeline> CreateGraphicsPipeline() const final { return nullptr; }
	};

	class BodySubPass final : public myvk_rg::GraphicsPassBase {
	private:
		MYVK_RG_OBJECT_FRIENDS
		inline void Initialize(myvk_rg::ImageInput prev_level_img, uint32_t level) {
			auto level_img = CreateResource<myvk_rg::ManagedImage>({"level"}, VK_FORMAT_R32_SFLOAT);
			level_img->SetCanvasSize(level, 1);
			AddColorAttachmentInput<0, myvk_rg::Usage::kColorAttachmentW>({"level"}, level_img);
			AddDescriptorInput<0, myvk_rg::Usage::kSampledImage, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT>(
			    {"prev_level"}, prev_level_img,
			    myvk::Sampler::Create(GetRenderGraphPtr()->GetDevicePtr(), VK_FILTER_NEAREST,
			                          VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));
		}

	public:
		inline auto GetLevelOutput() { return MakeImageOutput({"level"}); }
		inline void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const final {}
		inline myvk::Ptr<myvk::GraphicsPipeline> CreateGraphicsPipeline() const final { return nullptr; }
	};

	uint32_t m_levels{};
	std::vector<myvk_rg::ImageInput> m_images;
	void set_levels(uint32_t levels, myvk_rg::ImageInput depth_img) {
		m_levels = levels;
		ClearPasses();
		if (levels == 0)
			return;
		auto top_pass = CreatePass<TopSubPass>({"level", 0}, depth_img);
		for (uint32_t i = 1; i < levels; ++i) {
			CreatePass<BodySubPass>(
			    {"level", i},
			    i == 1 ? top_pass->GetTopOutput() : GetPass<BodySubPass>({"level", i - 1})->GetLevelOutput(), i);
		}
	}

	MYVK_RG_OBJECT_FRIENDS
	inline void Initialize(myvk_rg::ImageInput depth_img) { set_levels(10, depth_img); }

public:
	auto GetDepthHierarchyOutput() {
		std::vector<myvk_rg::ImageOutput> images(m_levels);
		images[0] = GetPass<TopSubPass>({"level", 0})->GetTopOutput();
		for (uint32_t i = 1; i < m_levels; ++i)
			images[i] = GetPass<BodySubPass>({"level", i})->GetLevelOutput();
		return MakeCombinedImage({"depth_hierarchy"}, VK_IMAGE_VIEW_TYPE_2D, std::move(images));
	}
};

/* class DrawListPass final
    : public myvk_rg::Pass<DrawListPass, myvk_rg::PassFlag::kDescriptor | myvk_rg::PassFlag::kCompute, true> {
private:
public:
    inline void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) final {}
}; */

class GBufferPass final : public myvk_rg::GraphicsPassBase {
private:
	MYVK_RG_OBJECT_FRIENDS
	inline void Initialize(myvk_rg::BufferInput draw_list) {
		auto depth = CreateResource<myvk_rg::ManagedImage>({"depth"}, VK_FORMAT_D32_SFLOAT);
		auto albedo = CreateResource<myvk_rg::ManagedImage>({"albedo"}, VK_FORMAT_R8G8B8A8_UNORM);
		auto normal = CreateResource<myvk_rg::ManagedImage>({"normal"}, VK_FORMAT_R8G8B8A8_SNORM);
		auto bright = CreateResource<myvk_rg::ManagedImage>({"bright"}, VK_FORMAT_B10G11R11_UFLOAT_PACK32);
		auto last_frame_albedo = MakeLastFrameImage({"last_frame_albedo"}, albedo);

		AddColorAttachmentInput<0, myvk_rg::Usage::kColorAttachmentW>({"albedo"}, albedo);
		AddColorAttachmentInput<1, myvk_rg::Usage::kColorAttachmentW>({"normal"}, normal);
		AddColorAttachmentInput<2, myvk_rg::Usage::kColorAttachmentW>({"bright"}, bright);

		AddInputAttachmentInput<0, 0>({"last_frame_albedo"}, last_frame_albedo);
		SetDepthAttachmentInput<myvk_rg::Usage::kDepthAttachmentRW>({"depth"}, depth);
		AddInput<myvk_rg::Usage::kDrawIndirectBuffer>({"draw_list"}, draw_list);
	}

public:
	inline auto GetAlbedoOutput() { return MakeImageOutput({"albedo"}); }
	inline auto GetNormalOutput() { return MakeImageOutput({"normal"}); }
	inline auto GetBrightOutput() { return MakeImageOutput({"bright"}); }
	inline auto GetDepthOutput() { return MakeImageOutput({"depth"}); }
	inline void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const final {}
	inline myvk::Ptr<myvk::GraphicsPipeline> CreateGraphicsPipeline() const final { return nullptr; }
};

class WBOITGenPass final : public myvk_rg::GraphicsPassBase {
private:
	MYVK_RG_OBJECT_FRIENDS
	inline void Initialize(myvk_rg::ImageInput depth_test_img) {
		auto reveal = CreateResource<myvk_rg::ManagedImage>({"reveal"}, VK_FORMAT_B10G11R11_UFLOAT_PACK32);
		auto accum = CreateResource<myvk_rg::ManagedImage>({"accum"}, VK_FORMAT_R32_SFLOAT);
		reveal->SetLoadOp(VK_ATTACHMENT_LOAD_OP_CLEAR);
		reveal->SetCanvasSize();

		AddColorAttachmentInput<0, myvk_rg::Usage::kColorAttachmentRW>({"reveal"}, reveal);
		AddColorAttachmentInput<1, myvk_rg::Usage::kColorAttachmentRW>({"accum"}, accum);
		SetDepthAttachmentInput<myvk_rg::Usage::kDepthAttachmentR>({"depth_test"}, depth_test_img);
		// Should be an assertion fail here
		// MakeImageOutput({"depth_test"});
	}

public:
	inline auto GetRevealOutput() { return MakeImageOutput({"reveal"}); }
	inline auto GetAccumOutput() { return MakeImageOutput({"accum"}); }
	inline void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const final {}
	inline myvk::Ptr<myvk::GraphicsPipeline> CreateGraphicsPipeline() const final { return nullptr; }
};

class BlurPass final : public myvk_rg::PassGroupBase {
private:
	class Subpass final : public myvk_rg::GraphicsPassBase {
	private:
		MYVK_RG_OBJECT_FRIENDS
		inline void Initialize(myvk_rg::ImageInput image_src) {
			printf("image_src = %p\n", image_src);

			auto image_dst = CreateResource<myvk_rg::ManagedImage>({"image_dst"}, image_src->GetFormat());
			AddDescriptorInput<0, myvk_rg::Usage::kSampledImage, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT>(
			    {"image_src"}, image_src,
			    myvk::Sampler::Create(GetRenderGraphPtr()->GetDevicePtr(), VK_FILTER_LINEAR,
			                          VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));
			AddColorAttachmentInput<0, myvk_rg::Usage::kColorAttachmentW>({"image_dst"}, image_dst);

			// auto descriptor_set_layout = GetDescriptorSetLayout();
		}

	public:
		inline ~Subpass() final = default;

		inline auto GetImageDstOutput() { return MakeImageOutput({"image_dst"}); }

		inline void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const final {}
		inline myvk::Ptr<myvk::GraphicsPipeline> CreateGraphicsPipeline() const final { return nullptr; }
	};

	uint32_t m_subpass_count = 10;

	MYVK_RG_OBJECT_FRIENDS
	inline void Initialize(myvk_rg::ImageInput image_src) {
		for (uint32_t i = 0; i < m_subpass_count; ++i) {
			CreatePass<Subpass>({"blur_subpass", i},
			                    i == 0 ? image_src : GetPass<Subpass>({"blur_subpass", i - 1})->GetImageDstOutput());
		}
	}

public:
	~BlurPass() final = default;

	inline auto GetImageDstOutput() {
		return MakeImageAliasOutput({"image_dst"},
		                            GetPass<Subpass>({"blur_subpass", m_subpass_count - 1})->GetImageDstOutput());
	}
};

class ScreenPass final : public myvk_rg::GraphicsPassBase {
private:
	MYVK_RG_OBJECT_FRIENDS
	inline void Initialize(myvk_rg::ImageInput screen_out, myvk_rg::ImageInput gbuffer_albedo,
	                           myvk_rg::ImageInput gbuffer_normal, myvk_rg::ImageInput wboit_reveal,
	                           myvk_rg::ImageInput wboit_accum) {
		AddInputAttachmentInput<0, 0>({"gbuffer_albedo"}, gbuffer_albedo);
		AddInputAttachmentInput<1, 1>({"gbuffer_normal"}, gbuffer_normal);
		AddInputAttachmentInput<2, 2>({"wboit_reveal"}, wboit_reveal);
		AddInputAttachmentInput<3, 3>({"wboit_accum"}, wboit_accum);
		AddColorAttachmentInput<0, myvk_rg::Usage::kColorAttachmentW>({"screen"}, screen_out);
	}

public:
	~ScreenPass() final = default;

	inline auto GetScreenOutput() { return MakeImageOutput({"screen"}); }
	inline void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const final {}
	inline myvk::Ptr<myvk::GraphicsPipeline> CreateGraphicsPipeline() const final { return nullptr; }
};

class BrightPass final : public myvk_rg::GraphicsPassBase {
private:
	MYVK_RG_OBJECT_FRIENDS
	inline void Initialize(myvk_rg::ImageInput screen_out, myvk_rg::ImageInput screen_in,
	                           myvk_rg::ImageInput blurred_bright) {
		AddInputAttachmentInput<0, 0>({"screen_in"}, screen_in);
		AddInputAttachmentInput<1, 1>({"blurred_bright"}, blurred_bright);
		AddColorAttachmentInput<0, myvk_rg::Usage::kColorAttachmentW>({"screen_out"}, screen_out);
	}

public:
	~BrightPass() final = default;

	inline auto GetScreenOutput() { return MakeImageOutput({"screen_out"}); }

	inline void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const final {}
	inline myvk::Ptr<myvk::GraphicsPipeline> CreateGraphicsPipeline() const final { return nullptr; }
};

class TestPass0 final : public myvk_rg::ComputePassBase {
private:
	MYVK_RG_OBJECT_FRIENDS
	inline void Initialize() {
		for (int i = 0; i < 3; ++i) {
			auto managed_buffer = CreateResource<myvk_rg::ManagedBuffer>({"draw_list", i});
			managed_buffer->SetSize(sizeof(uint32_t) * 100);
			std::cout << managed_buffer->GetKey().GetName() << " " << managed_buffer->GetKey().GetID() << std::endl;
			printf("GetBuffer: %p, GetImage: %p\n", GetBufferResource({"draw_list", i}),
			       GetImageResource({"draw_list", i}));

			auto managed_image = CreateResource<myvk_rg::ManagedImage>({"noise_tex", i}, VK_FORMAT_R8_UNORM);
			std::cout << managed_image->GetKey().GetName() << " " << managed_image->GetKey().GetID() << std::endl;
			printf("GetBuffer: %p, GetImage: %p\n", GetBufferResource({"noise_tex", i}),
			       GetImageResource({"noise_tex", i}));

			if (i) {
				RemoveInput({"draw_list_gen", i - 1});
				RemoveInput({"noise_tex", i - 1});
			}

			AddDescriptorInput<0, myvk_rg::Usage::kStorageBufferW, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT>(
			    {"draw_list_gen", i}, managed_buffer);
			// printf("input.usage = %d\ninput.resource = %p\n", input->GetUsage(),
			//    dynamic_cast<myvk_rg::ManagedBuffer *>(input->GetResource()));
			auto output_buffer = MakeBufferOutput({"draw_list_gen", i});
			printf("output_buffer = %p\n", output_buffer);
			output_buffer = MakeBufferOutput({"draw_list_gen", i});
			printf("output_buffer2 = %p\n", output_buffer);

			AddDescriptorInput<1, myvk_rg::Usage::kStorageImageW, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT>(
			    {"noise_tex", i}, managed_image);
			// printf("input.usage = %d\ninput.resource = %p\n", input->GetUsage(),
			//    dynamic_cast<myvk_rg::ManagedBuffer *>(input->GetResource()));
			// auto output_image_invalid = GetBufferOutput({"noise_tex", i});
			// printf("output_image_invalid = %p\n", output_image_invalid);
			auto output_image = MakeImageOutput({"noise_tex", i});
			printf("output_image = %p, id = %d\n", output_image, output_image->GetKey().GetID());
			output_image = MakeImageOutput({"noise_tex", i});
			printf("output_image = %p, id = %d, this = %p\n", output_image, output_image->GetKey().GetID(),
			       dynamic_cast<myvk_rg::_details_::PassBase *>(this));
		}

		printf("Create TestPass0\n");
	}

public:
	~TestPass0() final = default;

	void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const override {}
	inline myvk::Ptr<myvk::ComputePipeline> CreateComputePipeline() const final { return nullptr; }
	inline auto GetDrawListOutput() { return MakeBufferOutput({"draw_list_gen", 2}); }
	inline auto GetNoiseTexOutput() { return MakeImageOutput({"noise_tex", 2}); }
};

class TestPass1 final : public myvk_rg::GraphicsPassBase {
private:
	MYVK_RG_OBJECT_FRIENDS
	inline void Initialize(myvk_rg::BufferInput draw_list, myvk_rg::ImageInput noise_tex) {
		printf("Create TestPass\n");
		AddDescriptorInput<0, myvk_rg::Usage::kStorageBufferR,
		                   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT>(
		    {"draw_list_rw"}, draw_list);
		// AddDescriptorInput<1, myvk_rg::Usage::kStorageImageR, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT>(
		//     {"noise_tex_rw"}, noise_tex);
		AddInputAttachmentInput<0, 1>({"noise_tex_rw"}, noise_tex);

		auto final_img = CreateResource<myvk_rg::ManagedImage>({"final_img"}, VK_FORMAT_R8G8B8A8_UNORM);
		printf("final_img = %p\n", final_img);
		AddColorAttachmentInput<0, myvk_rg::Usage::kColorAttachmentW>({"color_attachment"}, final_img);
	}

public:
	~TestPass1() final = default;

	void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const override {}
	inline myvk::Ptr<myvk::GraphicsPipeline> CreateGraphicsPipeline() const final { return nullptr; }

	inline auto GetColorOutput() { return MakeImageOutput({"color_attachment"}); }
};

class TestRenderGraph final : public myvk_rg::RenderGraph<TestRenderGraph> {
private:
	MYVK_RG_RENDER_GRAPH_FRIENDS
	inline void Initialize(const myvk::Ptr<myvk::FrameManager> &frame_manager) {
		{
			auto test_pass_0 = CreatePass<TestPass0>({"test_pass_0"});
			auto test_blur_pass = CreatePass<BlurPass>({"test_blur_pass"}, test_pass_0->GetNoiseTexOutput());

			printf("blur_output = %p\n", test_blur_pass->GetImageDstOutput());
			printf("blur_output = %p\n", test_blur_pass->GetImageDstOutput());
			printf("blur_output = %p\n", test_blur_pass->GetImageDstOutput());

			auto test_pass_1 = CreatePass<TestPass1>({"test_pass_1"}, test_pass_0->GetDrawListOutput(),
			                                         test_blur_pass->GetImageDstOutput());
			printf("color_output = %p\n", test_pass_1->GetColorOutput());
			// AddResult({"output"}, test_pass_1->GetColorOutput());
		}
		{
			auto swapchain_image = CreateResource<myvk_rg::SwapchainImage>({"swapchain_image"}, frame_manager);

			auto last_frame_depth = MakeLastFrameImage({"lf_depth"});
			auto depth_hierarchy_pass = CreatePass<DepthHierarchyPass>({"depth_hierarchy_pass"}, last_frame_depth);

			auto cull_pass = CreatePass<CullPass>({"cull_pass"}, depth_hierarchy_pass->GetDepthHierarchyOutput());
			auto gbuffer_pass = CreatePass<GBufferPass>({"gbuffer_pass"}, cull_pass->GetDrawListOutput());

			last_frame_depth->SetCurrentResource(gbuffer_pass->GetDepthOutput());

			auto blur_bright_pass = CreatePass<BlurPass>({"blur_bright_pass"}, gbuffer_pass->GetBrightOutput());
			auto wboit_gen_pass = CreatePass<WBOITGenPass>({"wboit_gen_pass"}, gbuffer_pass->GetDepthOutput());
			auto screen_pass = CreatePass<ScreenPass>(
			    {"screen_pass"}, CreateResource<myvk_rg::ManagedImage>({"screen1"}, VK_FORMAT_R8G8B8A8_UNORM),
			    gbuffer_pass->GetAlbedoOutput(), gbuffer_pass->GetNormalOutput(), wboit_gen_pass->GetRevealOutput(),
			    wboit_gen_pass->GetAccumOutput());
			auto bright_pass = CreatePass<BrightPass>({"bright_pass"}, swapchain_image, screen_pass->GetScreenOutput(),
			                                          blur_bright_pass->GetImageDstOutput());

			AddResult({"final"}, bright_pass->GetScreenOutput());
		}
	}

public:
	void ToggleResult1() {
		if (IsResultExist({"output"}))
			RemoveResult({"output"});
		else {
			AddResult({"output"}, GetPass<TestPass1>({"test_pass_1"})->GetColorOutput());
		}
	}
};
#endif

class GaussianBlurPass final : public myvk_rg::PassGroupBase {
private:
	template <const uint32_t ProgramSpv[], std::size_t ProgramSize>
	class GaussianBlurSubpass final : public myvk_rg::GraphicsPassBase {
	private:
		myvk::Ptr<myvk::GraphicsPipeline> m_pipeline;

	public:
		inline void Initialize(myvk_rg::ImageInput image, VkFormat format) {
			AddDescriptorInput<0, myvk_rg::Usage::kSampledImage, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT>(
			    {"in"}, image,
			    myvk::Sampler::CreateClampToBorder(GetRenderGraphPtr()->GetDevicePtr(), VK_FILTER_LINEAR, {}));
			auto out_img = CreateResource<myvk_rg::ManagedImage>({"out"}, format);
			AddColorAttachmentInput<0, myvk_rg::Usage::kColorAttachmentW>({"out"}, out_img);
		}
		inline auto GetImageOutput() { return MakeImageOutput({"out"}); }
		inline void CreatePipeline() final {
			auto pipeline_layout =
			    myvk::PipelineLayout::Create(GetRenderGraphPtr()->GetDevicePtr(), {GetVkDescriptorSetLayout()}, {});
			myvk::GraphicsPipelineState pipeline_state = {};
			auto extent = GetRenderGraphPtr()->GetCanvasSize();
			pipeline_state.m_viewport_state.Enable(
			    std::vector<VkViewport>{{0, 0, (float)extent.width, (float)extent.height}},
			    std::vector<VkRect2D>{{{0, 0}, extent}});
			pipeline_state.m_vertex_input_state.Enable();
			pipeline_state.m_input_assembly_state.Enable(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
			pipeline_state.m_rasterization_state.Initialize(VK_POLYGON_MODE_FILL, VK_FRONT_FACE_COUNTER_CLOCKWISE,
			                                                VK_CULL_MODE_FRONT_BIT);
			pipeline_state.m_multisample_state.Enable(VK_SAMPLE_COUNT_1_BIT);
			pipeline_state.m_color_blend_state.Enable(1, VK_FALSE);

			static constexpr uint32_t kQuadVertSpv[] = {
#include "quad.vert.u32"
			};

			auto vert_shader_module =
			    myvk::ShaderModule::Create(GetRenderGraphPtr()->GetDevicePtr(), kQuadVertSpv, sizeof(kQuadVertSpv));
			auto frag_shader_module =
			    myvk::ShaderModule::Create(GetRenderGraphPtr()->GetDevicePtr(), ProgramSpv, ProgramSize);
			std::vector<VkPipelineShaderStageCreateInfo> shader_stages = {
			    vert_shader_module->GetPipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT),
			    frag_shader_module->GetPipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT)};

			m_pipeline = myvk::GraphicsPipeline::Create(pipeline_layout, GetVkRenderPass(), shader_stages,
			                                            pipeline_state, GetSubpass());
		}
		inline void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const final {
			command_buffer->CmdBindPipeline(m_pipeline);
			command_buffer->CmdBindDescriptorSets({GetVkDescriptorSet()}, m_pipeline);
			command_buffer->CmdDraw(3, 1, 0, 0);
		}
	};

	inline static constexpr uint32_t kBlurXSpv[] = {
#include "blur_x.frag.u32"
	};
	inline static constexpr uint32_t kBlurYSpv[] = {
#include "blur_y.frag.u32"
	};
	using BlurXSubpass = GaussianBlurSubpass<kBlurXSpv, sizeof(kBlurXSpv)>;
	using BlurYSubpass = GaussianBlurSubpass<kBlurYSpv, sizeof(kBlurYSpv)>;

public:
	inline void Initialize(myvk_rg::ImageInput image, VkFormat format) {
		auto blur_x_pass = CreatePass<BlurXSubpass>({"blur_x"}, image, format);
		auto blur_y_pass = CreatePass<BlurYSubpass>({"blur_y"}, blur_x_pass->GetImageOutput(), format);
		CreateImageAliasOutput({"image"}, GetPass<BlurYSubpass>({"blur_y"})->GetImageOutput());
	}
	inline auto GetImageOutput() { return GetImageAliasOutput({"image"}); }
};

class DimPass final : public myvk_rg::GraphicsPassBase {
private:
	myvk::Ptr<myvk::GraphicsPipeline> m_pipeline;

	float m_dim{0.99f};

public:
	inline void Initialize(myvk_rg::ImageInput image, VkFormat format) {
		AddInputAttachmentInput<0, 0>({"in"}, image);
		auto out_image = CreateResource<myvk_rg::ManagedImage>({"out"}, format);
		AddColorAttachmentInput<0, myvk_rg::Usage::kColorAttachmentW>({"out"}, out_image);
	}
	inline void CreatePipeline() final {
		// Not the best solution, just to test INPUT_ATTACHMENT
		auto pipeline_layout =
		    myvk::PipelineLayout::Create(GetRenderGraphPtr()->GetDevicePtr(), {GetVkDescriptorSetLayout()},
		                                 {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float)}});

		myvk::GraphicsPipelineState pipeline_state = {};
		auto extent = GetRenderGraphPtr()->GetCanvasSize();
		pipeline_state.m_viewport_state.Enable(
		    std::vector<VkViewport>{{0, 0, (float)extent.width, (float)extent.height}},
		    std::vector<VkRect2D>{{{0, 0}, extent}});
		pipeline_state.m_vertex_input_state.Enable();
		pipeline_state.m_input_assembly_state.Enable(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
		pipeline_state.m_rasterization_state.Initialize(VK_POLYGON_MODE_FILL, VK_FRONT_FACE_COUNTER_CLOCKWISE,
		                                                VK_CULL_MODE_FRONT_BIT);
		pipeline_state.m_multisample_state.Enable(VK_SAMPLE_COUNT_1_BIT);
		pipeline_state.m_color_blend_state.Enable(1, VK_FALSE);

		static constexpr uint32_t kQuadVertSpv[] = {
#include "quad.vert.u32"
		};
		static constexpr uint32_t kDimFragSpv[] = {
#include "dim.frag.u32"
		};

		auto vert_shader_module =
		    myvk::ShaderModule::Create(GetRenderGraphPtr()->GetDevicePtr(), kQuadVertSpv, sizeof(kQuadVertSpv));
		auto frag_shader_module =
		    myvk::ShaderModule::Create(GetRenderGraphPtr()->GetDevicePtr(), kDimFragSpv, sizeof(kDimFragSpv));
		std::vector<VkPipelineShaderStageCreateInfo> shader_stages = {
		    vert_shader_module->GetPipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT),
		    frag_shader_module->GetPipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT)};

		m_pipeline = myvk::GraphicsPipeline::Create(pipeline_layout, GetVkRenderPass(), shader_stages, pipeline_state,
		                                            GetSubpass());
	}
	inline void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const final {
		command_buffer->CmdBindPipeline(m_pipeline);
		command_buffer->CmdBindDescriptorSets({GetVkDescriptorSet()}, m_pipeline);
		command_buffer->CmdPushConstants(m_pipeline->GetPipelineLayoutPtr(), VK_SHADER_STAGE_FRAGMENT_BIT, 0,
		                                 sizeof(float), &m_dim);
		command_buffer->CmdDraw(3, 1, 0, 0);
	}
	inline auto GetImageOutput() { return MakeImageOutput({"out"}); }
	inline void SetDim(float dim) { m_dim = dim; }
};

class MyRenderGraph final : public myvk_rg::RenderGraph<MyRenderGraph> {
private:
	MYVK_RG_RENDER_GRAPH_FRIENDS
	void Initialize(const myvk::Ptr<myvk::FrameManager> &frame_manager) {
		/* auto init_image = CreateResource<myvk_rg::ManagedImage>({"init"}, VK_FORMAT_A2B10G10R10_UNORM_PACK32);
		init_image->SetLoadOp(VK_ATTACHMENT_LOAD_OP_CLEAR);
		init_image->SetClearColorValue({0.5f, 0, 0, 1}); */

		auto format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;

		auto lf_image = CreateResource<myvk_rg::LastFrameImage>({"lf"});

		auto blur_pass = CreatePass<GaussianBlurPass>({"blur_pass"}, lf_image, format);
		auto blur_pass2 = CreatePass<GaussianBlurPass>({"blur_pass2"}, blur_pass->GetImageOutput(), format);

		auto dim_pass = CreatePass<DimPass>({"dim_pass"}, blur_pass2->GetImageOutput(), format);

		auto imgui_pass = CreatePass<myvk_rg::ImGuiPass>({"imgui_pass"}, dim_pass->GetImageOutput());

		lf_image->SetCurrentResource(imgui_pass->GetImageOutput());

		auto swapchain_image = CreateResource<myvk_rg::SwapchainImage>({"swapchain_image"}, frame_manager);
		swapchain_image->SetLoadOp(VK_ATTACHMENT_LOAD_OP_DONT_CARE);

		auto copy_pass = CreatePass<myvk_rg::ImageBlitPass>({"blit_pass"}, imgui_pass->GetImageOutput(),
		                                                    swapchain_image, VK_FILTER_NEAREST);

		AddResult({"final"}, copy_pass->GetDstOutput());
	}

public:
	inline void SetDim(float dim) { GetPass<DimPass>({"dim_pass"})->SetDim(dim); }
};

int main() {
	GLFWwindow *window = myvk::GLFWCreateWindow("Test", 640, 480, true);

	myvk::Ptr<myvk::Device> device;
	myvk::Ptr<myvk::Queue> generic_queue;
	myvk::Ptr<myvk::PresentQueue> present_queue;
	{
		auto instance = myvk::Instance::CreateWithGlfwExtensions();
		auto surface = myvk::Surface::Create(instance, window);
		auto physical_device = myvk::PhysicalDevice::Fetch(instance)[0];
		device = myvk::Device::Create(
		    physical_device, myvk::GenericPresentQueueSelector{&generic_queue, surface, &present_queue},
		    physical_device->GetDefaultFeatures(), {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_SWAPCHAIN_EXTENSION_NAME});
	}
	myvk::ImGuiInit(window, myvk::CommandPool::Create(generic_queue));

	auto frame_manager =
	    myvk::FrameManager::Create(generic_queue, present_queue, false, kFrameCount,
	                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

	myvk::Ptr<MyRenderGraph> render_graphs[kFrameCount];
	for (auto &render_graph : render_graphs) {
		render_graph = MyRenderGraph::Create(generic_queue, frame_manager);
		render_graph->SetCanvasSize(frame_manager->GetExtent());
	}
	frame_manager->SetResizeFunc([](const VkExtent2D &extent) {});

	float dim_level = 1.0;
	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();

		myvk::ImGuiNewFrame();
		ImGui::Begin("Dim");
		ImGui::DragFloat("Dim Level", &dim_level, 0.1f, 1.0, 10000.0);
		ImGui::End();
		ImGui::Begin("Test");
		ImGui::Text("%f", ImGui::GetIO().Framerate);
		ImGui::End();
		ImGui::Render();

		if (frame_manager->NewFrame()) {
			uint32_t image_index = frame_manager->GetCurrentImageIndex();
			uint32_t current_frame = frame_manager->GetCurrentFrame();
			const auto &command_buffer = frame_manager->GetCurrentCommandBuffer();
			const auto &render_graph = render_graphs[current_frame];

			command_buffer->Begin();

			render_graph->SetCanvasSize(frame_manager->GetExtent());
			render_graph->SetDim(1.0f - std::exp(-dim_level));
			render_graph->CmdExecute(command_buffer);

			command_buffer->End();

			frame_manager->Render();
		}
	}

	frame_manager->WaitIdle();
	glfwTerminate();
	return 0;
}