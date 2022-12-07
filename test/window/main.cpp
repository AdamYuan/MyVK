#include <iostream>

#include "myvk/FrameManager.hpp"
#include "myvk/GLFWHelper.hpp"
#include "myvk/ImGuiHelper.hpp"
#include "myvk/ImGuiRenderer.hpp"
#include "myvk/Instance.hpp"
#include "myvk/Queue.hpp"
#include "myvk/RenderGraph2.hpp"

constexpr uint32_t kFrameCount = 3;

class TestPass0 final : public myvk::render_graph::RGPassBase,
                        public myvk::render_graph::RGResourcePool<TestPass0>,
                        public myvk::render_graph::RGInputPool<TestPass0> {
public:
	void Create() {
		printf("Create TestPass0\n");
		auto managed_buffer = CreateResource<myvk::render_graph::RGManagedBuffer>("draw_list");
		std::cout << managed_buffer->GetName() << std::endl;
		printf("GetBuffer: %p, GetImage: %p\n", GetBufferResource("draw_list"), GetImageResource("draw_list"));

		auto input = AddInput<myvk::render_graph::RGInputUsage::kStorageBufferW>("draw_list_gen", managed_buffer);
		printf("input.usage = %d\ninput.resource = %p\n", input->GetUsage(),
		       dynamic_cast<myvk::render_graph::RGManagedBuffer *>(input->GetResource()));
		auto output_buffer = CreateBufferOutput("draw_list_gen");
		printf("output_buffer = %p\n", output_buffer);
		auto output_buffer2 = CreateBufferOutput("draw_list_gen");
		printf("output_buffer2 = %p\n", output_buffer2);
	}
	myvk::render_graph::RGBufferBase *GetDrawListOutput() { return CreateBufferOutput("draw_list_gen"); }
};

class TestPass final : public myvk::render_graph::RGPassBase,
                       public myvk::render_graph::RGResourcePool<TestPass>,
                       public myvk::render_graph::RGInputPool<TestPass> {
public:
	void Create(myvk::render_graph::RGBufferBase *draw_list) {
		printf("Create TestPass\n");
		auto input = AddInput<myvk::render_graph::RGInputUsage::kStorageBufferRW>("draw_list_rw", draw_list);
		printf("input.usage = %d\ninput.resource = %p\n", input->GetUsage(),
		       dynamic_cast<myvk::render_graph::RGManagedBuffer *>(input->GetResource()));
		auto output_buffer = CreateBufferOutput("draw_list_rw");
		printf("output_buffer = %p\n", output_buffer);
		auto output_buffer2 = CreateBufferOutput("draw_list_rw");
		printf("output_buffer2 = %p\n", output_buffer2);
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
	test_pass.Create(test_pass0.GetDrawListOutput());

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