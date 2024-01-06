#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <myvk_rg/executor/DefaultExecutor.hpp>
#include <myvk_rg/interface/Key.hpp>
#include <myvk_rg/interface/Pool.hpp>
#include <myvk_rg/interface/Resource.hpp>

TEST_CASE("Test Key") {
	using myvk_rg::interface::GlobalKey;
	using myvk_rg::interface::PoolKey;
	PoolKey key0 = {"pass", 0};
	PoolKey key1 = {"albedo_tex"};
	GlobalKey global_key0 = GlobalKey{GlobalKey{key0}, key1};
	GlobalKey global_key1 = GlobalKey{GlobalKey{key0}, key1};
	CHECK_EQ(global_key0.Format(), "pass:0.albedo_tex");
	CHECK_EQ(key0.Format(), "pass:0");
	CHECK((key0 != key1));
	CHECK((GlobalKey{global_key0, key0} != global_key0));
	CHECK((global_key0 == global_key1));
}

TEST_CASE("Test Pool Data") {
	using myvk_rg::interface::ExternalBufferBase;
	using myvk_rg::interface::ManagedBuffer;
	using myvk_rg::interface::PoolData;
	using myvk_rg::interface::Value;
	using myvk_rg::interface::Variant;
	using myvk_rg::interface::Wrapper;

	Wrapper<int> int_wrapper;
	CHECK_EQ(*int_wrapper.Construct<int>(10), 10);
	CHECK_EQ(*int_wrapper.Get<int>(), 10);

	Wrapper<std::istream> ifs_wrapper;
	CHECK(ifs_wrapper.Construct<std::ifstream>());
	CHECK_EQ(ifs_wrapper.Get<std::istringstream>(), nullptr);
	CHECK(ifs_wrapper.Get<std::istream>());
	CHECK(ifs_wrapper.Get<std::ifstream>());

	Wrapper<Variant<int, double, std::istream>> var_wrapper;
	CHECK(var_wrapper.Construct<std::ifstream>());
	CHECK_EQ(var_wrapper.Get<int>(), nullptr);
	CHECK_EQ(var_wrapper.Get<std::istringstream>(), nullptr);
	CHECK(var_wrapper.Get<std::istream>());
	CHECK(var_wrapper.Get<std::ifstream>());

	CHECK_EQ(*var_wrapper.Construct<int>(2), 2);
	CHECK_EQ(var_wrapper.Get<std::istream>(), nullptr);
	CHECK_EQ(var_wrapper.Get<double>(), nullptr);
	CHECK_EQ(*var_wrapper.Get<int>(), 2);
	*var_wrapper.Get<int>() = 3;
	CHECK_EQ(*var_wrapper.Get<int>(), 3);
}

#include <myvk_rg/RenderGraph.hpp>

class GaussianBlurPass final : public myvk_rg::PassGroupBase {
private:
	template <const uint32_t ProgramSpv[], std::size_t ProgramSize>
	class GaussianBlurSubpass final : public myvk_rg::GraphicsPassBase {
	public:
		inline GaussianBlurSubpass(myvk_rg::Parent parent, const myvk_rg::Image &image, VkFormat format)
		    : myvk_rg::GraphicsPassBase(parent) {
			AddDescriptorInput<0, myvk_rg::Usage::kSampledImage, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT>({"in"}, image,
			                                                                                              nullptr);
			auto out_img = CreateResource<myvk_rg::ManagedImage>({"out"}, format);
			AddColorAttachmentInput<0, myvk_rg::Usage::kColorAttachmentW>({"out"}, out_img->GetAlias());
		}
		inline ~GaussianBlurSubpass() final = default;
		inline void CreatePipeline() final {}
		inline void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const final {}
		inline auto GetImageOutput() { return MakeImageOutput({"out"}); }
	};

	inline static constexpr uint32_t kBlurXSpv[] = {};
	inline static constexpr uint32_t kBlurYSpv[] = {};

public:
	using BlurXSubpass = GaussianBlurSubpass<kBlurXSpv, sizeof(kBlurXSpv)>;
	using BlurYSubpass = GaussianBlurSubpass<kBlurYSpv, sizeof(kBlurYSpv)>;

	inline GaussianBlurPass(myvk_rg::Parent parent, const myvk_rg::Image &image, VkFormat format)
	    : myvk_rg::PassGroupBase(parent) {
		auto blur_x_pass = CreatePass<BlurXSubpass>({"blur_x"}, image, format);
		CreatePass<BlurYSubpass>({"blur_y"}, blur_x_pass->GetImageOutput(), format);
	}
	inline auto GetImageOutput() { return GetPass<BlurYSubpass>({"blur_y"})->GetImageOutput(); }
};

class DimPass final : public myvk_rg::GraphicsPassBase {
private:
	myvk::Ptr<myvk::GraphicsPipeline> m_pipeline;

	float m_dim{0.99f};

public:
	inline DimPass(myvk_rg::Parent parent, const myvk_rg::Image &image, VkFormat format)
	    : myvk_rg::GraphicsPassBase(parent) {
		AddInputAttachmentInput<0, 0>({"in"}, image);
		auto out_image = CreateResource<myvk_rg::ManagedImage>({"out"}, format);
		AddColorAttachmentInput<0, myvk_rg::Usage::kColorAttachmentW>({"out"}, out_image->GetAlias());
	}
	inline ~DimPass() final = default;
	inline void CreatePipeline() final {}
	inline void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const final {}
	inline auto GetImageOutput() { return MakeImageOutput({"out"}); }
	inline void SetDim(float dim) { m_dim = dim; }
};

class MyRenderGraph final : public myvk_rg::RenderGraphBase<> {
public:
	inline MyRenderGraph() {
		auto format = VK_FORMAT_R32G32B32A32_SFLOAT;

		auto lf_image = CreateResource<myvk_rg::LastFrameImage>({"lf"});
		/* lf_image->SetInitTransferFunc(
		    [](const myvk::Ptr<myvk::CommandBuffer> &command_buffer, const myvk::Ptr<myvk::ImageView> &image_view) {
		        command_buffer->CmdClearColorImage(image_view->GetImagePtr(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                                           {{1.0, 0.0, 0.0, 1.0}});
		    }); */

		auto blur_pass = CreatePass<GaussianBlurPass::BlurYSubpass>({"blur_pass"}, lf_image->GetAlias(), format);
		auto blur_pass2 = CreatePass<GaussianBlurPass>({"blur_pass2"}, blur_pass->GetImageOutput(), format);

		auto dim_pass = CreatePass<DimPass>({"dim_pass"}, blur_pass2->GetImageOutput(), format);

		lf_image->SetCurrentResource(dim_pass->GetImageOutput());

		AddResult({"final"}, dim_pass->GetImageOutput());
	}
	inline ~MyRenderGraph() final = default;

	inline void SetDim(float dim) { GetPass<DimPass>({"dim_pass"})->SetDim(dim); }
	inline void ReInitBG(const float rgb[3]) {
		GetResource<myvk_rg::LastFrameImage>({"lf"})->SetInitTransferFunc(
		    [r = rgb[0], g = rgb[1], b = rgb[2]](const myvk::Ptr<myvk::CommandBuffer> &command_buffer,
		                                         const myvk::Ptr<myvk::ImageBase> &image) {
			    command_buffer->CmdClearColorImage(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, {{r, g, b, 1.0}});
		    });
	}
};

#include "../../src/rg/executor/default/Collector.hpp"
TEST_CASE("Test Default Resolver") {
	auto render_graph = myvk::MakePtr<MyRenderGraph>();
	Collector collector;
	collector.Collect(*render_graph);
	printf("PASSES:\n");
	for (const auto &it : collector.GetPasses())
		printf("%s -> %p\n", it.first.Format().c_str(), it.second);
	printf("INPUTS:\n");
	for (const auto &it : collector.GetInputs())
		printf("%s -> %p\n", it.first.Format().c_str(), it.second);
	printf("RESOURCES:\n");
	for (const auto &it : collector.GetResources())
		printf("%s -> %p\n", it.first.Format().c_str(), it.second);
}
