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
			    myvk::Sampler::Create(GetRenderGraphPtr()->GetDevicePtr(), VK_FILTER_LINEAR,
			                          VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));
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

public:
	using BlurXSubpass = GaussianBlurSubpass<kBlurXSpv, sizeof(kBlurXSpv)>;
	using BlurYSubpass = GaussianBlurSubpass<kBlurYSpv, sizeof(kBlurYSpv)>;

	inline void Initialize(myvk_rg::ImageInput image, VkFormat format) {
		auto blur_x_pass = CreatePass<BlurXSubpass>({"blur_x"}, image, format);
		auto blur_y_pass = CreatePass<BlurYSubpass>({"blur_y"}, blur_x_pass->GetImageOutput(), format);
		CreateImageAliasOutput({"image"}, blur_y_pass->GetImageOutput());
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

		auto format = VK_FORMAT_R32G32B32A32_SFLOAT;

		auto lf_image = CreateResource<myvk_rg::LastFrameImage>({"lf"});
		/* lf_image->SetInitTransferFunc(
		    [](const myvk::Ptr<myvk::CommandBuffer> &command_buffer, const myvk::Ptr<myvk::ImageView> &image_view) {
		        command_buffer->CmdClearColorImage(image_view->GetImagePtr(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                                           {{1.0, 0.0, 0.0, 1.0}});
		    }); */

		auto blur_pass = CreatePass<GaussianBlurPass::BlurYSubpass>({"blur_pass"}, lf_image, format);
		auto blur_pass2 = CreatePass<GaussianBlurPass>({"blur_pass2"}, blur_pass->GetImageOutput(), format);

		auto dim_pass = CreatePass<DimPass>({"dim_pass"}, blur_pass2->GetImageOutput(), format);

		auto imgui_pass = CreatePass<myvk_rg::ImGuiPass>({"imgui_pass"}, blur_pass->GetImageOutput());

		lf_image->SetCurrentResource(imgui_pass->GetImageOutput());

		auto swapchain_image = CreateResource<myvk_rg::SwapchainImage>({"swapchain_image"}, frame_manager);
		swapchain_image->SetLoadOp(VK_ATTACHMENT_LOAD_OP_DONT_CARE);

		auto copy_pass = CreatePass<myvk_rg::ImageBlitPass>({"blit_pass"}, imgui_pass->GetImageOutput(),
		                                                    swapchain_image, VK_FILTER_NEAREST);

		AddResult({"final"}, copy_pass->GetDstOutput());
	}

public:
	inline void SetDim(float dim) { GetPass<DimPass>({"dim_pass"})->SetDim(dim); }
	inline void ReInitBG(const float rgb[3]) {
		GetResource<myvk_rg::LastFrameImage>({"lf"})->SetInitTransferFunc(
		    [r = rgb[0], g = rgb[1], b = rgb[2]](const myvk::Ptr<myvk::CommandBuffer> &command_buffer,
		                                         const myvk::Ptr<myvk::ImageBase> &image) {
			    command_buffer->CmdClearColorImage(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, {{r, g, b, 1.0}});
		    });
	}
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

	float init_rgb[3] = {1.0f, 0.0f, 0.0f};
	float dim_level = 5.0;
	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();

		myvk::ImGuiNewFrame();
		ImGui::Begin("Config");
		ImGui::DragFloat("Dim Level", &dim_level, 0.1f, 1.0, 10.0);
		ImGui::ColorPicker3("Init Color", init_rgb);
		if (ImGui::Button("Re-Init")) {
			for (const auto &rg : render_graphs)
				rg->ReInitBG(init_rgb);
		}
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