#include <iostream>

#include "myvk/FrameManager.hpp"
#include "myvk/GLFWHelper.hpp"
#include "myvk/ImGuiHelper.hpp"
#include "myvk/ImGuiRenderer.hpp"
#include "myvk/Instance.hpp"
#include "myvk/Queue.hpp"
#include "myvk/RenderGraph2.hpp"

constexpr uint32_t kFrameCount = 3;

class TestPass0 final
    : public myvk::render_graph::RGPass<TestPass0, myvk::render_graph::RGPassFlag::kEnableAllAllocation>,
      public myvk::render_graph::RGPool<
          TestPass0, int, myvk::render_graph::RGPoolVariant<int, double>,
          myvk::render_graph::RGPoolVariant<myvk::render_graph::RGBufferBase, myvk::render_graph::RGBufferAlias>> {
public:
	using TestPool = myvk::render_graph::RGPool<
	    TestPass0, int, myvk::render_graph::RGPoolVariant<int, double>,
	    myvk::render_graph::RGPoolVariant<myvk::render_graph::RGBufferBase, myvk::render_graph::RGBufferAlias>>;

	void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) override {}

	void Create() {
		auto *subpass = CreatePass<TestPass0>({"123"});
		std::cout << "subpass name = " << subpass->GetKey().GetName() << std::endl;
		DeletePass({"123"});

		for (int i = 0; i < 3; ++i) {
			auto managed_buffer = CreateResource<myvk::render_graph::RGManagedBuffer>({"draw_list", i});
			std::cout << managed_buffer->GetKey().GetName() << " " << managed_buffer->GetKey().GetID() << std::endl;
			printf("GetBuffer: %p, GetImage: %p\n", GetBufferResource({"draw_list", i}),
			       GetImageResource({"draw_list", i}));

			auto managed_image = CreateResource<myvk::render_graph::RGManagedImage>({"noise_tex", i});
			std::cout << managed_image->GetKey().GetName() << " " << managed_image->GetKey().GetID() << std::endl;
			printf("GetBuffer: %p, GetImage: %p\n", GetBufferResource({"noise_tex", i}),
			       GetImageResource({"noise_tex", i}));

			AddInput<myvk::render_graph::RGUsage::kStorageBufferW, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT>({"draw_list_gen", i}, managed_buffer);
			// printf("input.usage = %d\ninput.resource = %p\n", input->GetUsage(),
			//    dynamic_cast<myvk::render_graph::RGManagedBuffer *>(input->GetResource()));
			auto output_buffer = GetBufferOutput({"draw_list_gen", i});
			printf("output_buffer = %p\n", output_buffer);
			output_buffer = GetBufferOutput({"draw_list_gen", i});
			printf("output_buffer2 = %p\n", output_buffer);

			AddInput<myvk::render_graph::RGUsage::kColorAttachmentW>({"noise_tex", i}, managed_image);
			// printf("input.usage = %d\ninput.resource = %p\n", input->GetUsage(),
			//    dynamic_cast<myvk::render_graph::RGManagedBuffer *>(input->GetResource()));
			auto output_image_invalid = GetBufferOutput({"noise_tex", i});
			printf("output_image_invalid = %p\n", output_image_invalid);
			auto output_image = GetImageOutput({"noise_tex", i});
			printf("output_image = %p\n", output_image);
			output_image = GetImageOutput({"noise_tex", i});
			printf("output_image = %p, producer_pass = %p, this = %p\n", output_image,
			       output_image->GetProducerPassPtr(), dynamic_cast<myvk::render_graph::RGPassBase *>(this));
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

		TestPool::template Initialize<2, myvk::render_graph::RGBufferAlias>({"Int"}, managed_buffer);
		printf("Value: RGBufferBase =%p, RGBufferAlias =%p\n",
		       TestPool::template Get<2, myvk::render_graph::RGBufferBase>({"Int"}),
		       TestPool::template Get<2, myvk::render_graph::RGBufferAlias>({"Int"}));

		TestPool::template Initialize<2, myvk::render_graph::RGManagedBuffer>({"Int"});
		printf("Value: RGManagedBuffer =%p, RGBufferAlias =%p; IsInitialized=%d\n",
		       TestPool::template Get<2, myvk::render_graph::RGManagedBuffer>({"Int"}),
		       TestPool::template Get<2, myvk::render_graph::RGBufferAlias>({"Int"}),
		       TestPool::template IsInitialized<2>({"Int"}));

		TestPool::template Reset<2>({"Int"});
		printf("Value: RGManagedBuffer =%p, RGBufferAlias =%p; IsInitialized=%d\n",
		       TestPool::template Get<2, myvk::render_graph::RGManagedBuffer>({"Int"}),
		       TestPool::template Get<2, myvk::render_graph::RGBufferAlias>({"Int"}),
		       TestPool::template IsInitialized<2>({"Int"}));

		printf("Create TestPass0\n");
	}
	myvk::render_graph::RGBufferBase *GetDrawListOutput() { return GetBufferOutput({"draw_list_gen"}); }
	myvk::render_graph::RGImageBase *GetNoiseTexOutput() { return GetImageOutput({"noise_tex"}); }
};

class TestPass final
    : public myvk::render_graph::RGPass<TestPass, myvk::render_graph::RGPassFlag::kEnableAllAllocation> {
public:
	void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) override {}

	void Create(myvk::render_graph::RGBufferBase *draw_list, myvk::render_graph::RGImageBase *noise_tex) {
		printf("Create TestPass\n");
		AddInput<myvk::render_graph::RGUsage::kStorageBufferRW, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT>({"draw_list_rw"}, draw_list);
		AddInput<myvk::render_graph::RGUsage::kStorageImageRW, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT>({"noise_tex_rw"}, noise_tex);

		AddDescriptorSet({"set"}, {{{"draw_list_rw"}, VK_SHADER_STAGE_VERTEX_BIT},
		                           {{"noise_tex_rw"}, VK_SHADER_STAGE_FRAGMENT_BIT}});
		// printf("input.usage = %d\ninput.resource = %p\n", input->GetUsage(),
		//       dynamic_cast<myvk::render_graph::RGManagedBuffer *>(input->GetResource()));
		auto output_buffer = GetBufferOutput({"draw_list_rw"});
		printf("output_buffer = %p\n", output_buffer);
		std::cout << output_buffer->GetKey().GetName() << std::endl;
		auto output_buffer2 = GetBufferOutput({"draw_list_rw"});
		printf("output_buffer2 = %p\n", output_buffer2);
		std::cout << output_buffer2->GetKey().GetName() << std::endl;

		auto output_image = GetImageOutput({"noise_tex_rw"});
		printf("output_image = %p\n", output_image);
		std::cout << output_image->GetKey().GetName() << std::endl;
		output_image = GetImageOutput({"noise_tex_rw"});
		printf("output_image = %p, producer_pass = %p, this = %p\n", output_image, output_image->GetProducerPassPtr(),
		       dynamic_cast<myvk::render_graph::RGPassBase *>(this));
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

	TestPass0 test_pass0;
	test_pass0.Create();
	TestPass test_pass;
	test_pass.Create(test_pass0.GetDrawListOutput(), test_pass0.GetNoiseTexOutput());

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