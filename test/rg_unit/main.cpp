#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <myvk_rg/executor/DefaultExecutor.hpp>
#include <myvk_rg/interface/Input.hpp>
#include <myvk_rg/interface/Key.hpp>
#include <myvk_rg/interface/Pool.hpp>
#include <myvk_rg/interface/Resource.hpp>

TEST_SUITE("Interface") {
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
		using namespace myvk_rg::interface;

		Wrapper<int> int_wrapper;
		CHECK_EQ(*int_wrapper.Construct<int>(10), 10);
		CHECK_EQ(*int_wrapper.Get<int>(), 10);

		Wrapper<int> i2{std::move(int_wrapper)};

		Wrapper<ObjectBase> ifs_wrapper;
		CHECK(ifs_wrapper.Construct<ManagedBuffer>(Parent{}));
		CHECK_EQ(ifs_wrapper.Get<ManagedImage>(), nullptr);
		CHECK(ifs_wrapper.Get<BufferBase>());
		CHECK(ifs_wrapper.Get<ResourceBase>());

		CHECK(std::is_move_constructible_v<Value<ManagedBuffer>>);
		CHECK(std::is_move_assignable_v<Value<ManagedBuffer>>);
		CHECK(std::is_copy_constructible_v<Value<ManagedBuffer>>);
		CHECK(std::is_copy_assignable_v<Value<ManagedBuffer>>);

		CHECK(std::is_move_constructible_v<Value<ObjectBase>>);
		CHECK(std::is_move_assignable_v<Value<ObjectBase>>);
		CHECK_FALSE(std::is_copy_constructible_v<Value<ObjectBase>>);
		CHECK_FALSE(std::is_copy_assignable_v<Value<ObjectBase>>);

		Wrapper<Variant<int, double, ResourceBase>> var_wrapper;
		CHECK(var_wrapper.Construct<ManagedBuffer>(Parent{}));
		CHECK_EQ(var_wrapper.Get<int>(), nullptr);
		CHECK_EQ(var_wrapper.Get<ManagedImage>(), nullptr);
		CHECK(var_wrapper.Get<ObjectBase>());
		CHECK(var_wrapper.Get<BufferBase>());

		CHECK_EQ(*var_wrapper.Construct<int>(2), 2);
		CHECK_EQ(var_wrapper.Get<BufferBase>(), nullptr);
		CHECK_EQ(var_wrapper.Get<double>(), nullptr);
		CHECK_EQ(*var_wrapper.Get<int>(), 2);
		*var_wrapper.Get<int>() = 3;
		CHECK_EQ(*var_wrapper.Get<int>(), 3);
	}
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
			AddColorAttachmentInput<0, myvk_rg::Usage::kColorAttachmentW>({"out"}, out_img->AsInput());
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
		AddColorAttachmentInput<0, myvk_rg::Usage::kColorAttachmentW>({"out"}, out_image->AsInput());
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

		auto blur_pass = CreatePass<GaussianBlurPass::BlurYSubpass>({"blur_pass"}, lf_image->AsInput(), format);
		auto blur_pass2 = CreatePass<GaussianBlurPass>({"blur_pass2"}, blur_pass->GetImageOutput(), format);

		auto dim_pass = CreatePass<DimPass>({"dim_pass"}, blur_pass2->GetImageOutput(), format);

		lf_image->SetPointedAlias(dim_pass->GetImageOutput());

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

#include "../../src/rg/executor/default/Collection.hpp"
#include "../../src/rg/executor/default/Dependency.hpp"
TEST_SUITE("Default Executor") {
	auto render_graph = myvk::MakePtr<MyRenderGraph>();

	using default_executor::Collection;
	using default_executor::Dependency;

	using myvk_rg::interface::PassBase;
	using myvk_rg::interface::ResourceBase;

	Collection collection;
	TEST_CASE("Test Collection") {
		auto res = Collection::Create(*render_graph);
		CHECK_MESSAGE(res.IsOK(), res.PopError().Format());
		collection = res.PopValue();
		printf("PASSES:\n");
		for (const auto &it : collection.GetPasses())
			printf("%s\n", it.first.Format().c_str());
		printf("INPUTS:\n");
		for (const auto &it : collection.GetInputs()) {
			printf("%s -> %d, %s\n", it.first.Format().c_str(), static_cast<int>(it.second->GetInputAlias().GetState()),
			       it.second->GetInputAlias().GetSourceKey().Format().c_str());
		}
		printf("RESOURCES:\n");
		for (const auto &it : collection.GetResources())
			printf("%s\n", it.first.Format().c_str());
	}

	Dependency dependency;
	TEST_CASE("Test Dependency") {
		auto res = Dependency::Create({.render_graph = *render_graph, .collection = collection});
		CHECK_MESSAGE(res.IsOK(), res.PopError().Format());
		dependency = res.PopValue();

		dependency.GetPassGraph().WriteGraphViz(
		    std::cout,
		    [](const PassBase *p_pass) {
			    return p_pass ? p_pass->GetGlobalKey().Format() + ";" +
			                        std::to_string(Dependency::GetPassTopoOrder(p_pass))
			                  : "start";
		    },
		    [](const Dependency::PassEdge &e) {
			    return e.p_resource->GetGlobalKey().Format() + (e.type == Dependency::EdgeType::kLocal ? "" : "(LF)");
		    });

		dependency.GetResourceGraph().WriteGraphViz(
		    std::cout, [](const ResourceBase *p_resource) { return p_resource->GetGlobalKey().Format(); },
		    [](const Dependency::ResourceEdge &e) { return e.type == Dependency::EdgeType::kLocal ? "" : "LF"; });

		{
			auto kahn_result = dependency.GetPassGraph().KahnTopologicalSort(
			    [](const Dependency::PassEdge &e) { return e.type == Dependency::EdgeType::kLocal; },
			    std::initializer_list<const PassBase *>{nullptr});
			CHECK(kahn_result.is_dag);
			CHECK_EQ(kahn_result.sorted[0], nullptr);
			for (const auto p_pass : kahn_result.sorted) {
				std::cout << (p_pass ? p_pass->GetGlobalKey().Format() : "start") << std::endl;
			}
		}
		{
			auto kahn_result = dependency.GetPassGraph().KahnTopologicalSort(
			    [](const Dependency::PassEdge &e) { return true; }, std::initializer_list<const PassBase *>{nullptr});
			CHECK(!kahn_result.is_dag);
		}
		/* for (const auto &it : graph.GetPassNodes()) {
		    printf("\t%s:\n", it.first->GetGlobalKey().Format().c_str());
		    printf("\tInputs:\n");
		    for (const auto &in : it.second.input_nodes)
		        printf("\t\t%zu, %s\n", in->source_node.index(),
		               in->p_input->GetInputAlias().GetSourceKey().Format().c_str());
		    printf("\tOutputs:\n");
		    for (const auto &in : it.second.output_nodes)
		        printf("\t\t%s\n", in->p_input->GetInputAlias().GetSourceKey().Format().c_str());
		} */
	}
}
