#include <iostream>

#include "myvk/FrameManager.hpp"
#include "myvk/GLFWHelper.hpp"
#include "myvk/ImGuiHelper.hpp"
#include "myvk/ImGuiRenderer.hpp"
#include "myvk/Instance.hpp"
#include "myvk/Queue.hpp"
#include "myvk_rg/RenderGraph.hpp"

constexpr uint32_t kFrameCount = 3;

class CullPass final : public myvk_rg::ComputePass<CullPass> {
private:
	MYVK_RG_OBJECT_FRIENDS
	MYVK_RG_INLINE_INITIALIZER(myvk_rg::ImageInput depth_hierarchy) {
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
};

class DepthHierarchyPass final : public myvk_rg::PassGroup<DepthHierarchyPass> {
private:
	class TopSubPass final : public myvk_rg::GraphicsPass<TopSubPass> {
	private:
		MYVK_RG_OBJECT_FRIENDS
		MYVK_RG_INLINE_INITIALIZER(myvk_rg::ImageInput depth_img) {
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
	};

	class BodySubPass final : public myvk_rg::GraphicsPass<BodySubPass> {
	private:
		MYVK_RG_OBJECT_FRIENDS
		MYVK_RG_INLINE_INITIALIZER(myvk_rg::ImageInput prev_level_img, uint32_t level) {
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
	MYVK_RG_INLINE_INITIALIZER(myvk_rg::ImageInput depth_img) { set_levels(10, depth_img); }

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

class GBufferPass final : public myvk_rg::GraphicsPass<GBufferPass> {
private:
	MYVK_RG_OBJECT_FRIENDS
	MYVK_RG_INLINE_INITIALIZER(myvk_rg::BufferInput draw_list) {
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
};

class WBOITGenPass final : public myvk_rg::GraphicsPass<WBOITGenPass> {
private:
	MYVK_RG_OBJECT_FRIENDS
	MYVK_RG_INLINE_INITIALIZER(myvk_rg::ImageInput depth_test_img) {
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
};

class BlurPass final : public myvk_rg::PassGroup<BlurPass> {
private:
	class Subpass final : public myvk_rg::GraphicsPass<Subpass> {
	private:
		MYVK_RG_OBJECT_FRIENDS
		MYVK_RG_INLINE_INITIALIZER(myvk_rg::ImageInput image_src) {
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
	};

	uint32_t m_subpass_count = 10;

	MYVK_RG_OBJECT_FRIENDS
	MYVK_RG_INLINE_INITIALIZER(myvk_rg::ImageInput image_src) {
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

class ScreenPass final : public myvk_rg::GraphicsPass<ScreenPass> {
private:
	MYVK_RG_OBJECT_FRIENDS
	MYVK_RG_INLINE_INITIALIZER(myvk_rg::ImageInput screen_out, myvk_rg::ImageInput gbuffer_albedo,
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
};

class BrightPass final : public myvk_rg::GraphicsPass<BrightPass> {
private:
	MYVK_RG_OBJECT_FRIENDS
	MYVK_RG_INLINE_INITIALIZER(myvk_rg::ImageInput screen_out, myvk_rg::ImageInput screen_in,
	                           myvk_rg::ImageInput blurred_bright) {
		AddInputAttachmentInput<0, 0>({"screen_in"}, screen_in);
		AddInputAttachmentInput<1, 1>({"blurred_bright"}, blurred_bright);
		AddColorAttachmentInput<0, myvk_rg::Usage::kColorAttachmentW>({"screen_out"}, screen_out);
	}

public:
	~BrightPass() final = default;

	inline auto GetScreenOutput() { return MakeImageOutput({"screen_out"}); }
	inline void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const final {}
};

class TestPass0 final : public myvk_rg::ComputePass<TestPass0> {
private:
	MYVK_RG_OBJECT_FRIENDS
	MYVK_RG_INLINE_INITIALIZER() {
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
	inline auto GetDrawListOutput() { return MakeBufferOutput({"draw_list_gen", 2}); }
	inline auto GetNoiseTexOutput() { return MakeImageOutput({"noise_tex", 2}); }
};

class TestPass1 final : public myvk_rg::GraphicsPass<TestPass1> {
private:
	MYVK_RG_OBJECT_FRIENDS
	MYVK_RG_INLINE_INITIALIZER(myvk_rg::BufferInput draw_list, myvk_rg::ImageInput noise_tex) {
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

	inline auto GetColorOutput() { return MakeImageOutput({"color_attachment"}); }
};

class TestRenderGraph final : public myvk_rg::RenderGraph<TestRenderGraph> {
private:
	MYVK_RG_RENDER_GRAPH_FRIENDS
	MYVK_RG_INLINE_INITIALIZER(const myvk::Ptr<myvk::FrameManager> &frame_manager) {
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

	auto frame_manager = myvk::FrameManager::Create(generic_queue, present_queue, false, kFrameCount);

	myvk::Ptr<TestRenderGraph> render_graphs[kFrameCount];
	for (auto &render_graph : render_graphs) {
		render_graph = myvk_rg::RenderGraph<TestRenderGraph>::Create(device, frame_manager);
		render_graph->SetCanvasSize(frame_manager->GetExtent());
	}
	frame_manager->SetResizeFunc([&render_graphs](const VkExtent2D &extent) {
		for (auto &render_graph : render_graphs) {
			render_graph->SetCanvasSize(extent);
		}
	});

	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();

		/* myvk::ImGuiNewFrame();
		ImGui::Begin("Test");
		ImGui::Text("%f", ImGui::GetIO().Framerate);
		ImGui::End();
		ImGui::Render(); */

		if (frame_manager->NewFrame()) {
			uint32_t image_index = frame_manager->GetCurrentImageIndex();
			uint32_t current_frame = frame_manager->GetCurrentFrame();
			const auto &command_buffer = frame_manager->GetCurrentCommandBuffer();

			command_buffer->Begin();

			render_graphs[current_frame]->CmdExecute(command_buffer);

			command_buffer->End();

			frame_manager->Render();
		}
	}

	frame_manager->WaitIdle();
	glfwTerminate();
	return 0;
}