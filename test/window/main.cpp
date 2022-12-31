#include <iostream>

#include "myvk/FrameManager.hpp"
#include "myvk/GLFWHelper.hpp"
#include "myvk/ImGuiHelper.hpp"
#include "myvk/ImGuiRenderer.hpp"
#include "myvk/Instance.hpp"
#include "myvk/Queue.hpp"
#include "myvk_rg/RenderGraph.hpp"

constexpr uint32_t kFrameCount = 3;

class GBufferPass final
    : public myvk_rg::Pass<GBufferPass, myvk_rg::PassFlag::kDescriptor | myvk_rg::PassFlag::kGraphics, true> {
private:
	MYVK_RG_OBJECT_FRIENDS
	MYVK_RG_INLINE_INITIALIZER() {
		auto depth = CreateResource<myvk_rg::ManagedImage>({"depth"});
		auto albedo = CreateResource<myvk_rg::ManagedImage>({"albedo"});
		auto normal = CreateResource<myvk_rg::ManagedImage>({"normal"});
		auto bright = CreateResource<myvk_rg::ManagedImage>({"bright"});
		AddColorAttachmentInput<0, myvk_rg::Usage::kColorAttachmentW>({"albedo"}, albedo);
		AddColorAttachmentInput<1, myvk_rg::Usage::kColorAttachmentW>({"normal"}, normal);
		AddColorAttachmentInput<2, myvk_rg::Usage::kColorAttachmentW>({"bright"}, bright);
		SetDepthAttachmentInput<myvk_rg::Usage::kDepthAttachmentRW>({"depth"}, depth);
	}

public:
	inline auto GetAlbedoOutput() { return MakeImageOutput({"albedo"}); }
	inline auto GetNormalOutput() { return MakeImageOutput({"normal"}); }
	inline auto GetBrightOutput() { return MakeImageOutput({"bright"}); }
	inline auto GetDepthOutput() { return MakeImageOutput({"depth"}); }
	inline void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) final {}
};

class WBOITGenPass final
    : public myvk_rg::Pass<WBOITGenPass, myvk_rg::PassFlag::kDescriptor | myvk_rg::PassFlag::kGraphics, true> {
private:
	MYVK_RG_OBJECT_FRIENDS
	MYVK_RG_INLINE_INITIALIZER(myvk_rg::Image *depth_test_img) {
		auto reveal = CreateResource<myvk_rg::ManagedImage>({"reveal"});
		auto accum = CreateResource<myvk_rg::ManagedImage>({"accum"});
		reveal->SetLoadOp(VK_ATTACHMENT_LOAD_OP_CLEAR);
		AddColorAttachmentInput<0, myvk_rg::Usage::kColorAttachmentRW>({"reveal"}, reveal);
		AddColorAttachmentInput<1, myvk_rg::Usage::kColorAttachmentRW>({"accum"}, accum);
		SetDepthAttachmentInput<myvk_rg::Usage::kDepthAttachmentR>({"depth_test"}, depth_test_img);
		// Should be an assertion fail here
		// MakeImageOutput({"depth_test"});
	}

public:
	inline auto GetRevealOutput() { return MakeImageOutput({"reveal"}); }
	inline auto GetAccumOutput() { return MakeImageOutput({"accum"}); }
	inline void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) final {}
};

class BlurSubpass final
    : public myvk_rg::Pass<BlurSubpass, myvk_rg::PassFlag::kDescriptor | myvk_rg::PassFlag::kGraphics, true> {
private:
	MYVK_RG_OBJECT_FRIENDS
	MYVK_RG_INLINE_INITIALIZER(myvk_rg::Image *image_src) {
		printf("image_src = %p\n", image_src);

		auto image_dst = CreateResource<myvk_rg::ManagedImage>({"image_dst"});
		AddDescriptorInput<0, myvk_rg::Usage::kSampledImage, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT>(
		    {"image_src"}, image_src, nullptr);
		AddColorAttachmentInput<0, myvk_rg::Usage::kColorAttachmentW>({"image_dst"}, image_dst);

		auto descriptor_set_layout = GetDescriptorSetLayout();
	}

public:
	inline ~BlurSubpass() final = default;

	inline myvk_rg::Image *GetImageDstOutput() { return MakeImageOutput({"image_dst"}); }

	inline void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) final {}
};

class BlurPass final : public myvk_rg::PassGroup<BlurPass> {
private:
	uint32_t m_subpass_count = 10;

	MYVK_RG_OBJECT_FRIENDS
	MYVK_RG_INLINE_INITIALIZER(myvk_rg::Image *image_src) {
		for (uint32_t i = 0; i < m_subpass_count; ++i) {
			PushPass<BlurSubpass>({"blur_subpass", i},
			                      i == 0 ? image_src
			                             : GetPass<BlurSubpass>({"blur_subpass", i - 1})->GetImageDstOutput());
		}
	}

public:
	~BlurPass() final = default;

	inline myvk_rg::Image *GetImageDstOutput() {
		return MakeImageAliasOutput({"image_dst"},
		                            GetPass<BlurSubpass>({"blur_subpass", m_subpass_count - 1})->GetImageDstOutput());
	}
};

class ScreenPass final
    : public myvk_rg::Pass<ScreenPass, myvk_rg::PassFlag::kDescriptor | myvk_rg::PassFlag::kGraphics> {
private:
	MYVK_RG_OBJECT_FRIENDS
	MYVK_RG_INLINE_INITIALIZER(myvk_rg::Image *screen_out, myvk_rg::Image *gbuffer_albedo,
	                           myvk_rg::Image *gbuffer_normal, myvk_rg::Image *wboit_reveal,
	                           myvk_rg::Image *wboit_accum) {
		AddInputAttachmentInput<0, 0>({"gbuffer_albedo"}, gbuffer_albedo);
		AddInputAttachmentInput<1, 1>({"gbuffer_normal"}, gbuffer_normal);
		AddInputAttachmentInput<2, 2>({"wboit_reveal"}, wboit_reveal);
		AddInputAttachmentInput<3, 3>({"wboit_accum"}, wboit_accum);
		AddColorAttachmentInput<0, myvk_rg::Usage::kColorAttachmentW>({"screen"}, screen_out);
	}

public:
	~ScreenPass() final = default;

	inline auto GetScreenOutput() { return MakeImageOutput({"screen"}); }
	inline void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) final {}
};

class BrightPass final
    : public myvk_rg::Pass<BrightPass, myvk_rg::PassFlag::kDescriptor | myvk_rg::PassFlag::kGraphics> {
private:
	MYVK_RG_OBJECT_FRIENDS
	MYVK_RG_INLINE_INITIALIZER(myvk_rg::Image *screen_out, myvk_rg::Image *screen_in, myvk_rg::Image *blurred_bright) {
		AddInputAttachmentInput<0, 0>({"screen_in"}, screen_in);
		AddInputAttachmentInput<1, 1>({"blurred_bright"}, blurred_bright);
		AddColorAttachmentInput<0, myvk_rg::Usage::kColorAttachmentW>({"screen_out"}, screen_out);
	}

public:
	~BrightPass() final = default;

	inline auto GetScreenOutput() { return MakeImageOutput({"screen_out"}); }
	inline void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) final {}
};

class TestPass0 final
    : public myvk_rg::Pass<TestPass0, myvk_rg::PassFlag::kDescriptor | myvk_rg::PassFlag::kGraphics, true> {
private:
	MYVK_RG_OBJECT_FRIENDS
	MYVK_RG_INLINE_INITIALIZER() {
		for (int i = 0; i < 3; ++i) {
			auto managed_buffer = CreateResource<myvk_rg::ManagedBuffer>({"draw_list", i});
			std::cout << managed_buffer->GetKey().GetName() << " " << managed_buffer->GetKey().GetID() << std::endl;
			printf("GetBuffer: %p, GetImage: %p\n", GetBufferResource({"draw_list", i}),
			       GetImageResource({"draw_list", i}));

			auto managed_image = CreateResource<myvk_rg::ManagedImage>({"noise_tex", i});
			std::cout << managed_image->GetKey().GetName() << " " << managed_image->GetKey().GetID() << std::endl;
			printf("GetBuffer: %p, GetImage: %p\n", GetBufferResource({"noise_tex", i}),
			       GetImageResource({"noise_tex", i}));

			if (i) {
				RemoveInput({"draw_list_gen", i - 1});
				RemoveInput({"noise_tex", i - 1});
			}

			AddDescriptorInput<0, myvk_rg::Usage::kStorageBufferW, VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT>(
			    {"draw_list_gen", i}, managed_buffer);
			// printf("input.usage = %d\ninput.resource = %p\n", input->GetUsage(),
			//    dynamic_cast<myvk_rg::ManagedBuffer *>(input->GetResource()));
			auto output_buffer = MakeBufferOutput({"draw_list_gen", i});
			printf("output_buffer = %p\n", output_buffer);
			output_buffer = MakeBufferOutput({"draw_list_gen", i});
			printf("output_buffer2 = %p\n", output_buffer);

			AddColorAttachmentInput<0, myvk_rg::Usage::kColorAttachmentW>({"noise_tex", i}, managed_image);
			// printf("input.usage = %d\ninput.resource = %p\n", input->GetUsage(),
			//    dynamic_cast<myvk_rg::ManagedBuffer *>(input->GetResource()));
			// auto output_image_invalid = GetBufferOutput({"noise_tex", i});
			// printf("output_image_invalid = %p\n", output_image_invalid);
			auto output_image = MakeImageOutput({"noise_tex", i});
			printf("output_image = %p, id = %d\n", output_image, output_image->GetKey().GetID());
			output_image = MakeImageOutput({"noise_tex", i});
			printf("output_image = %p, id = %d, producer_pass = %p, this = %p\n", output_image,
			       output_image->GetKey().GetID(), output_image->GetProducerPassPtr(),
			       dynamic_cast<myvk_rg::_details_::PassBase *>(this));
		}

		printf("Create TestPass0\n");
	}

public:
	~TestPass0() final = default;

	void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) override {}
	myvk_rg::Buffer *GetDrawListOutput() { return MakeBufferOutput({"draw_list_gen", 2}); }
	myvk_rg::Image *GetNoiseTexOutput() { return MakeImageOutput({"noise_tex", 2}); }
};

class TestPass1 final
    : public myvk_rg::Pass<TestPass1, myvk_rg::PassFlag::kDescriptor | myvk_rg::PassFlag::kGraphics, true> {
private:
	MYVK_RG_OBJECT_FRIENDS
	MYVK_RG_INLINE_INITIALIZER(myvk_rg::Buffer *draw_list, myvk_rg::Image *noise_tex) {
		printf("Create TestPass\n");
		AddDescriptorInput<0, myvk_rg::Usage::kStorageBufferR,
		                   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT>(
		    {"draw_list_rw"}, draw_list);
		// AddDescriptorInput<1, myvk_rg::Usage::kStorageImageR, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT>(
		//     {"noise_tex_rw"}, noise_tex);
		AddInputAttachmentInput<0, 1>({"noise_tex_rw"}, noise_tex);

		auto final_img = CreateResource<myvk_rg::ManagedImage>({"final_img"});
		printf("final_img = %p\n", final_img);
		AddColorAttachmentInput<0, myvk_rg::Usage::kColorAttachmentW>({"color_attachment"}, final_img);
	}

public:
	~TestPass1() final = default;

	void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) override {}

	auto *GetColorOutput() { return MakeImageOutput({"color_attachment"}); }
};

class TestRenderGraph final : public myvk_rg::RenderGraph<TestRenderGraph> {
private:
	MYVK_RG_RENDER_GRAPH_FRIENDS
	MYVK_RG_INLINE_INITIALIZER() {
		{
			auto test_pass_0 = PushPass<TestPass0>({"test_pass_0"});
			auto test_blur_pass = PushPass<BlurPass>({"test_blur_pass"}, test_pass_0->GetNoiseTexOutput());

			printf("blur_output = %p\n", test_blur_pass->GetImageDstOutput());
			printf("blur_output = %p\n", test_blur_pass->GetImageDstOutput());
			printf("blur_output = %p\n", test_blur_pass->GetImageDstOutput());

			auto test_pass_1 = PushPass<TestPass1>({"test_pass_1"}, test_pass_0->GetDrawListOutput(),
			                                       test_blur_pass->GetImageDstOutput());
			printf("color_output = %p\n", test_pass_1->GetColorOutput());
			// AddResult({"output"}, test_pass_1->GetColorOutput());
		}
		{
			auto gbuffer_pass = PushPass<GBufferPass>({"gbuffer_pass"});
			auto blur_bright_pass = PushPass<BlurPass>({"blur_bright_pass"}, gbuffer_pass->GetBrightOutput());
			auto wboit_gen_pass = PushPass<WBOITGenPass>({"wboit_gen_pass"}, gbuffer_pass->GetDepthOutput());
			auto screen_pass = PushPass<ScreenPass>(
			    {"screen_pass"}, CreateResource<myvk_rg::ManagedImage>({"screen1"}), gbuffer_pass->GetAlbedoOutput(),
			    gbuffer_pass->GetNormalOutput(), wboit_gen_pass->GetRevealOutput(), wboit_gen_pass->GetAccumOutput());
			auto bright_pass =
			    PushPass<BrightPass>({"bright_pass"}, CreateResource<myvk_rg::ManagedImage>({"screen2"}),
			                         screen_pass->GetScreenOutput(), blur_bright_pass->GetImageDstOutput());
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

int main() {
	GLFWwindow *window = myvk::GLFWCreateWindow("Test", 640, 480, true);
	myvk::ImGuiInit(window);

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

	myvk::Ptr<TestRenderGraph> render_graph = myvk_rg::RenderGraph<TestRenderGraph>::Create(device);
	render_graph->Compile();
	render_graph->ToggleResult1();
	printf("TOGGLE_RESULT_1\n");
	render_graph->Compile();
	render_graph->ToggleResult1();
	printf("TOGGLE_RESULT_1\n");
	render_graph->Compile();

	// object_pool.DeleteBuffer("draw_list");

	auto frame_manager = myvk::FrameManager::Create(generic_queue, present_queue, false, kFrameCount);

	myvk::Ptr<myvk::RenderPass> render_pass;
	{
		myvk::RenderPassState state{2, 1};
		state.RegisterAttachment(0, "color_attachment", frame_manager->GetSwapchain()->GetImageFormat(),
		                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_SAMPLE_COUNT_1_BIT,
		                         VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);

		state.RegisterSubpass(0, "color_pass").AddDefaultColorAttachment("color_attachment", nullptr);
		state.RegisterSubpass(1, "gui_pass").AddDefaultColorAttachment("color_attachment", "color_pass");

		render_pass = myvk::RenderPass::Create(device, state);
	}

	auto imgui_renderer =
	    myvk::ImGuiRenderer::Create(myvk::CommandPool::Create(generic_queue), render_pass, 1, kFrameCount);

	auto framebuffer = myvk::ImagelessFramebuffer::Create(render_pass, {frame_manager->GetSwapchainImageViews()[0]});
	frame_manager->SetResizeFunc([&framebuffer, &render_pass](const myvk::FrameManager &frame_manager) {
		framebuffer = myvk::ImagelessFramebuffer::Create(render_pass, {frame_manager.GetSwapchainImageViews()[0]});
	});

	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();

		myvk::ImGuiNewFrame();
		ImGui::Begin("Test");
		ImGui::Text("%f", ImGui::GetIO().Framerate);
		ImGui::End();
		ImGui::Render();

		if (frame_manager->NewFrame()) {
			uint32_t image_index = frame_manager->GetCurrentImageIndex();
			uint32_t current_frame = frame_manager->GetCurrentFrame();
			const auto &command_buffer = frame_manager->GetCurrentCommandBuffer();

			command_buffer->Begin();

			command_buffer->CmdBeginRenderPass(render_pass, {framebuffer},
			                                   {frame_manager->GetCurrentSwapchainImageView()},
			                                   {{{0.5f, 0.5f, 0.5f, 1.0f}}});
			// m_ray_tracer->CmdDrawPipeline(command_buffer, current_frame);
			command_buffer->CmdNextSubpass();
			imgui_renderer->CmdDrawPipeline(command_buffer, current_frame);
			command_buffer->CmdEndRenderPass();
			command_buffer->End();

			frame_manager->Render();
		}
	}

	frame_manager->WaitIdle();
	glfwTerminate();
	return 0;
}