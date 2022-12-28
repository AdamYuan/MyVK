#include <iostream>

#include "myvk/FrameManager.hpp"
#include "myvk/GLFWHelper.hpp"
#include "myvk/ImGuiHelper.hpp"
#include "myvk/ImGuiRenderer.hpp"
#include "myvk/Instance.hpp"
#include "myvk/Queue.hpp"
#include "myvk/RenderGraph2.hpp"

constexpr uint32_t kFrameCount = 3;

namespace myvk_rg = myvk::render_graph;

class GBufferPass final
    : public myvk_rg::RGPass<GBufferPass, myvk_rg::RGPassFlag::kDescriptor | myvk_rg::RGPassFlag::kGraphics, true> {
public:
	inline GBufferPass() {
		auto depth = CreateResource<myvk_rg::RGManagedImage>({"depth"});
		auto albedo = CreateResource<myvk_rg::RGManagedImage>({"albedo"});
		auto normal = CreateResource<myvk_rg::RGManagedImage>({"normal"});
		auto bright = CreateResource<myvk_rg::RGManagedImage>({"bright"});
		AddColorAttachmentInput<0, myvk_rg::RGUsage::kColorAttachmentW>({"albedo"}, albedo);
		AddColorAttachmentInput<1, myvk_rg::RGUsage::kColorAttachmentW>({"normal"}, normal);
		AddColorAttachmentInput<2, myvk_rg::RGUsage::kColorAttachmentW>({"bright"}, bright);
		SetDepthAttachmentInput<myvk_rg::RGUsage::kDepthAttachmentRW>({"depth"}, depth);
	}
	inline auto GetAlbedoOutput() { return MakeImageOutput({"albedo"}); }
	inline auto GetNormalOutput() { return MakeImageOutput({"normal"}); }
	inline auto GetBrightOutput() { return MakeImageOutput({"bright"}); }
	inline auto GetDepthOutput() { return MakeImageOutput({"depth"}); }
	inline void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) final {}
};

class WBOITGenPass final
    : public myvk_rg::RGPass<WBOITGenPass, myvk_rg::RGPassFlag::kDescriptor | myvk_rg::RGPassFlag::kGraphics, true> {
public:
	inline explicit WBOITGenPass(myvk_rg::RGImageBase *depth_test_img) {
		auto reveal = CreateResource<myvk_rg::RGManagedImage>({"reveal"});
		auto accum = CreateResource<myvk_rg::RGManagedImage>({"accum"});
		AddColorAttachmentInput<0, myvk_rg::RGUsage::kColorAttachmentRW>({"reveal"}, reveal);
		AddColorAttachmentInput<1, myvk_rg::RGUsage::kColorAttachmentRW>({"accum"}, accum);
		SetDepthAttachmentInput<myvk_rg::RGUsage::kDepthAttachmentR>({"depth_test"}, depth_test_img);

		// Should be an assertion fail here
		// MakeImageOutput({"depth_test"});
	}
	inline auto GetRevealOutput() { return MakeImageOutput({"reveal"}); }
	inline auto GetAccumOutput() { return MakeImageOutput({"accum"}); }
	inline void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) final {}
};

class BlurSubpass final
    : public myvk_rg::RGPass<BlurSubpass, myvk_rg::RGPassFlag::kDescriptor | myvk_rg::RGPassFlag::kGraphics, true> {
public:
	inline explicit BlurSubpass(myvk_rg::RGImageBase *image_src) {
		printf("image_src = %p\n", image_src);

		auto image_dst = CreateResource<myvk_rg::RGManagedImage>({"image_dst"});
		AddDescriptorInput<0, myvk_rg::RGUsage::kSampledImage, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT>(
		    {"image_src"}, image_src, nullptr);
		AddColorAttachmentInput<0, myvk_rg::RGUsage::kColorAttachmentW>({"image_dst"}, image_dst);
	}
	inline myvk_rg::RGImageBase *GetImageDstOutput() { return MakeImageOutput({"image_dst"}); }

	inline void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) final {}
};

class BlurPass final : public myvk_rg::RGPassGroup<BlurPass> {
private:
	uint32_t m_pass = 10;

public:
	inline explicit BlurPass(myvk_rg::RGImageBase *image_src) {
		for (uint32_t i = 0; i < m_pass; ++i) {
			PushPass<BlurSubpass>({"blur_subpass", i},
			                      i == 0 ? image_src
			                             : GetPass<BlurSubpass>({"blur_subpass", i - 1})->GetImageDstOutput());
		}
	}
	inline myvk_rg::RGImageBase *GetImageDstOutput() {
		return MakeImageAliasOutput({"image_dst"},
		                            GetPass<BlurSubpass>({"blur_subpass", m_pass - 1})->GetImageDstOutput());
	}
};

class ScreenPass final
    : public myvk_rg::RGPass<ScreenPass, myvk_rg::RGPassFlag::kDescriptor | myvk_rg::RGPassFlag::kGraphics> {
public:
	inline explicit ScreenPass(myvk_rg::RGImageBase *screen_out, myvk_rg::RGImageBase *gbuffer_albedo,
	                           myvk_rg::RGImageBase *gbuffer_normal, myvk_rg::RGImageBase *wboit_reveal,
	                           myvk_rg::RGImageBase *wboit_accum) {
		AddInputAttachmentInput<0, 0>({"gbuffer_albedo"}, gbuffer_albedo);
		AddInputAttachmentInput<1, 1>({"gbuffer_normal"}, gbuffer_normal);
		AddInputAttachmentInput<2, 2>({"wboit_reveal"}, wboit_reveal);
		AddInputAttachmentInput<3, 3>({"wboit_accum"}, wboit_accum);
		AddColorAttachmentInput<0, myvk_rg::RGUsage::kColorAttachmentW>({"screen"}, screen_out);
	}
	inline auto GetScreenOutput() { return MakeImageOutput({"screen"}); }
	inline void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) final {}
};

class BrightPass final
    : public myvk_rg::RGPass<BrightPass, myvk_rg::RGPassFlag::kDescriptor | myvk_rg::RGPassFlag::kGraphics> {
public:
	inline explicit BrightPass(myvk_rg::RGImageBase *screen_out, myvk_rg::RGImageBase *screen_in,
	                           myvk_rg::RGImageBase *blurred_bright) {
		AddInputAttachmentInput<0, 0>({"screen_in"}, screen_in);
		AddInputAttachmentInput<1, 1>({"blurred_bright"}, blurred_bright);
		AddColorAttachmentInput<0, myvk_rg::RGUsage::kColorAttachmentW>({"screen_out"}, screen_out);
	}
	inline auto GetScreenOutput() { return MakeImageOutput({"screen_out"}); }
	inline void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) final {}
};

class TestPass0 final
    : public myvk_rg::RGPass<TestPass0, myvk_rg::RGPassFlag::kDescriptor | myvk_rg::RGPassFlag::kGraphics, true>,
      public myvk_rg::RGPool<TestPass0, int, myvk_rg::RGPoolVariant<int, double>,
                             myvk_rg::RGPoolVariant<myvk_rg::RGBufferBase, myvk_rg::RGBufferAlias>> {
public:
	using TestPool = myvk_rg::RGPool<TestPass0, int, myvk_rg::RGPoolVariant<int, double>,
	                                 myvk_rg::RGPoolVariant<myvk_rg::RGBufferBase, myvk_rg::RGBufferAlias>>;

	void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) override {}

	TestPass0() {
		for (int i = 0; i < 3; ++i) {
			auto managed_buffer = CreateResource<myvk_rg::RGManagedBuffer>({"draw_list", i});
			std::cout << managed_buffer->GetKey().GetName() << " " << managed_buffer->GetKey().GetID() << std::endl;
			printf("GetBuffer: %p, GetImage: %p\n", GetBufferResource({"draw_list", i}),
			       GetImageResource({"draw_list", i}));

			auto managed_image = CreateResource<myvk_rg::RGManagedImage>({"noise_tex", i});
			std::cout << managed_image->GetKey().GetName() << " " << managed_image->GetKey().GetID() << std::endl;
			printf("GetBuffer: %p, GetImage: %p\n", GetBufferResource({"noise_tex", i}),
			       GetImageResource({"noise_tex", i}));

			if (i) {
				RemoveInput({"draw_list_gen", i - 1});
				RemoveInput({"noise_tex", i - 1});
			}

			AddDescriptorInput<0, myvk_rg::RGUsage::kStorageBufferW, VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT>(
			    {"draw_list_gen", i}, managed_buffer);
			// printf("input.usage = %d\ninput.resource = %p\n", input->GetUsage(),
			//    dynamic_cast<myvk_rg::RGManagedBuffer *>(input->GetResource()));
			auto output_buffer = MakeBufferOutput({"draw_list_gen", i});
			printf("output_buffer = %p\n", output_buffer);
			output_buffer = MakeBufferOutput({"draw_list_gen", i});
			printf("output_buffer2 = %p\n", output_buffer);

			AddColorAttachmentInput<0, myvk_rg::RGUsage::kColorAttachmentW>({"noise_tex", i}, managed_image);
			// printf("input.usage = %d\ninput.resource = %p\n", input->GetUsage(),
			//    dynamic_cast<myvk_rg::RGManagedBuffer *>(input->GetResource()));
			// auto output_image_invalid = GetBufferOutput({"noise_tex", i});
			// printf("output_image_invalid = %p\n", output_image_invalid);
			auto output_image = MakeImageOutput({"noise_tex", i});
			printf("output_image = %p, id = %d\n", output_image, output_image->GetKey().GetID());
			output_image = MakeImageOutput({"noise_tex", i});
			printf("output_image = %p, id = %d, producer_pass = %p, this = %p\n", output_image,
			       output_image->GetKey().GetID(), output_image->GetProducerPassPtr(),
			       dynamic_cast<myvk_rg::RGPassBase *>(this));
		}

		auto managed_buffer = GetBufferResource({"draw_list", 1});

		TestPool::template CreateAndInitialize<0, int>({"Int"}, 1);
		printf("Initialized: %d\n", TestPool::template IsInitialized<1>({"Int"}));
		TestPool::template Initialize<1, double>({"Int"}, 2.0);
		printf("Initialized: %d\n", TestPool::template IsInitialized<1>({"Int"}));
		printf("Value: p_int=%p, p_double=%p\n", TestPool::template Get<1, int>({"Int"}),
		       TestPool::template Get<1, double>({"Int"}));
		printf("Reset\n");
		TestPool::template Reset<1>({"Int"});
		printf("Initialized: %d\n", TestPool::template IsInitialized<1>({"Int"}));
		printf("Value: p_int=%p, p_double=%p\n", TestPool::template Get<1, int>({"Int"}),
		       TestPool::template Get<1, double>({"Int"}));
		TestPool::template Initialize<1, int>({"Int"}, 2);
		printf("Initialized: %d\n", TestPool::template IsInitialized<1>({"Int"}));
		printf("Value: p_int=%p, p_double=%p\n", TestPool::template Get<1, int>({"Int"}),
		       TestPool::template Get<1, double>({"Int"}));

		TestPool::template Initialize<2, myvk_rg::RGBufferAlias>({"Int"}, managed_buffer);
		printf("Value: RGBufferBase =%p, RGBufferAlias =%p\n",
		       TestPool::template Get<2, myvk_rg::RGBufferBase>({"Int"}),
		       TestPool::template Get<2, myvk_rg::RGBufferAlias>({"Int"}));

		TestPool::template Initialize<2, myvk_rg::RGManagedBuffer>({"Int"});
		printf("Value: RGManagedBuffer =%p, RGBufferAlias =%p; IsInitialized=%d\n",
		       TestPool::template Get<2, myvk_rg::RGManagedBuffer>({"Int"}),
		       TestPool::template Get<2, myvk_rg::RGBufferAlias>({"Int"}),
		       TestPool::template IsInitialized<2>({"Int"}));

		TestPool::template Reset<2>({"Int"});
		printf("Value: RGManagedBuffer =%p, RGBufferAlias =%p; IsInitialized=%d\n",
		       TestPool::template Get<2, myvk_rg::RGManagedBuffer>({"Int"}),
		       TestPool::template Get<2, myvk_rg::RGBufferAlias>({"Int"}),
		       TestPool::template IsInitialized<2>({"Int"}));

		printf("Create TestPass0\n");
	}
	myvk_rg::RGBufferBase *GetDrawListOutput() { return MakeBufferOutput({"draw_list_gen", 2}); }
	myvk_rg::RGImageBase *GetNoiseTexOutput() { return MakeImageOutput({"noise_tex", 2}); }
};

class TestPass1 final
    : public myvk_rg::RGPass<TestPass1, myvk_rg::RGPassFlag::kDescriptor | myvk_rg::RGPassFlag::kGraphics, true> {
public:
	void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) override {}

	TestPass1(myvk_rg::RGBufferBase *draw_list, myvk_rg::RGImageBase *noise_tex) {
		printf("Create TestPass\n");
		AddDescriptorInput<0, myvk_rg::RGUsage::kStorageBufferR,
		                   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT>(
		    {"draw_list_rw"}, draw_list);
		// AddDescriptorInput<1, myvk_rg::RGUsage::kStorageImageR, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT>(
		//     {"noise_tex_rw"}, noise_tex);
		AddInputAttachmentInput<0, 1>({"noise_tex_rw"}, noise_tex);

		auto final_img = CreateResource<myvk_rg::RGManagedImage>({"final_img"});
		printf("final_img = %p\n", final_img);
		AddColorAttachmentInput<0, myvk_rg::RGUsage::kColorAttachmentW>({"color_attachment"}, final_img);
	}

	auto *GetColorOutput() { return MakeImageOutput({"color_attachment"}); }
};

class TestRenderGraph final : public myvk_rg::RenderGraph<TestRenderGraph> {
public:
	TestRenderGraph() {
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
			    {"screen_pass"}, CreateResource<myvk_rg::RGManagedImage>({"screen1"}), gbuffer_pass->GetAlbedoOutput(),
			    gbuffer_pass->GetNormalOutput(), wboit_gen_pass->GetRevealOutput(), wboit_gen_pass->GetAccumOutput());
			auto bright_pass =
			    PushPass<BrightPass>({"bright_pass"}, CreateResource<myvk_rg::RGManagedImage>({"screen2"}),
			                         screen_pass->GetScreenOutput(), blur_bright_pass->GetImageDstOutput());
			AddResult({"final"}, bright_pass->GetScreenOutput());
		}
	}
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
	render_graph->gen_pass_sequence();
	render_graph->ToggleResult1();
	printf("TOGGLE_RESULT_1\n");
	render_graph->gen_pass_sequence();
	render_graph->ToggleResult1();
	printf("TOGGLE_RESULT_1\n");
	render_graph->gen_pass_sequence();

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