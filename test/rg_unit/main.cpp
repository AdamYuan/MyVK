#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <myvk_rg/executor/Executor.hpp>
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
			AddDescriptorInput<myvk_rg::Usage::kSampledImage, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT>({0}, {"in"},
			                                                                                           image, nullptr);
			auto out_img = CreateResource<myvk_rg::ManagedImage>({"out"}, format);
			AddColorAttachmentInput<myvk_rg::Usage::kColorAttachmentW>(0, {"out"}, out_img->Alias());
		}
		inline ~GaussianBlurSubpass() final = default;
		inline myvk::Ptr<myvk::GraphicsPipeline> CreatePipeline() const final { return nullptr; }
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
		AddInputAttachmentInput(0, {0}, {"in"}, image);
		auto out_image = CreateResource<myvk_rg::ManagedImage>({"out"}, format);
		AddColorAttachmentInput<myvk_rg::Usage::kColorAttachmentW>(0, {"out"}, out_image->Alias());
	}
	inline ~DimPass() final = default;
	inline myvk::Ptr<myvk::GraphicsPipeline> CreatePipeline() const final { return nullptr; }
	inline void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const final {}
	inline auto GetImageOutput() { return MakeImageOutput({"out"}); }
	inline void SetDim(float dim) { m_dim = dim; }
};

class MyRenderGraph final : public myvk_rg::RenderGraphBase {
public:
	inline MyRenderGraph() : myvk_rg::RenderGraphBase(nullptr) {
		auto format = VK_FORMAT_R32G32B32A32_SFLOAT;

		auto lf_image = CreateResource<myvk_rg::ManagedImage>({"lf"}, format);
		/* lf_image->SetInitTransferFunc(
		    [](const myvk::Ptr<myvk::CommandBuffer> &command_buffer, const myvk::Ptr<myvk::ImageView> &image_view) {
		        command_buffer->CmdClearColorImage(image_view->GetImagePtr(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                                           {{1.0, 0.0, 0.0, 1.0}});
		    }); */

		auto blur_pass = CreatePass<GaussianBlurPass::BlurYSubpass>({"blur_pass"}, lf_image->Alias(), format);
		auto blur_pass2 = CreatePass<GaussianBlurPass>({"blur_pass2"}, blur_pass->GetImageOutput(), format);
		auto combined_image = CreateResource<myvk_rg::CombinedImage>(
		    {"combined"}, VK_IMAGE_VIEW_TYPE_2D_ARRAY,
		    std::vector{blur_pass->GetImageOutput(), blur_pass2->GetImageOutput()});
		auto blur_pass3 = CreatePass<GaussianBlurPass>({"blur_pass3"}, combined_image->Alias(), format);

		auto dim_pass = CreatePass<DimPass>({"dim_pass"}, blur_pass3->GetImageOutput(), format);

		auto combined_image2 = CreateResource<myvk_rg::CombinedImage>(
		    {"combined2"}, VK_IMAGE_VIEW_TYPE_2D_ARRAY,
		    std::vector{dim_pass->GetImageOutput(), blur_pass3->GetImageOutput()});

		auto dim_pass2 = CreatePass<DimPass>({"dim_pass2"}, combined_image2->Alias(), format);

		AddResult({"final"}, dim_pass2->GetImageOutput());

		SetCanvasSize({1280, 720});
	}
	inline ~MyRenderGraph() final = default;

	inline void SetDim(float dim) { GetPass<DimPass>({"dim_pass"})->SetDim(dim); }
};

class InputAttPass final : public myvk_rg::GraphicsPassBase {
public:
	inline InputAttPass(myvk_rg::Parent parent, const myvk_rg::Image &image, VkFormat format)
	    : myvk_rg::GraphicsPassBase(parent) {
		AddInputAttachmentInput(0, {0}, {"in"}, image);
		auto out_image = CreateResource<myvk_rg::ManagedImage>({"out"}, format);
		AddColorAttachmentInput<myvk_rg::Usage::kColorAttachmentW>(0, {"out"}, out_image->Alias());
	}
	inline ~InputAttPass() final = default;
	inline myvk::Ptr<myvk::GraphicsPipeline> CreatePipeline() const final { return nullptr; }
	inline void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const final {}
	inline auto GetImageOutput() { return MakeImageOutput({"out"}); }
};

class SamplerPass final : public myvk_rg::GraphicsPassBase {
public:
	inline SamplerPass(myvk_rg::Parent parent, const myvk_rg::Image &image, VkFormat format)
	    : myvk_rg::GraphicsPassBase(parent) {
		AddDescriptorInput<myvk_rg::Usage::kSampledImage, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT>({0}, {"in"}, image,
		                                                                                           nullptr);
		auto out_image = CreateResource<myvk_rg::ManagedImage>({"out"}, format);
		AddColorAttachmentInput<myvk_rg::Usage::kColorAttachmentW>(0, {"out"}, out_image->Alias());
	}
	inline ~SamplerPass() final = default;
	inline myvk::Ptr<myvk::GraphicsPipeline> CreatePipeline() const final { return nullptr; }
	inline void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const final {}
	inline auto GetImageOutput() { return MakeImageOutput({"out"}); }
};

class ImageRPass final : public myvk_rg::ComputePassBase {
public:
	inline ImageRPass(myvk_rg::Parent parent, const myvk_rg::Image &image, VkFormat format)
	    : myvk_rg::ComputePassBase(parent) {
		AddDescriptorInput<myvk_rg::Usage::kStorageImageR, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT>({0}, {"in"}, image);
		auto out_image = CreateResource<myvk_rg::ManagedImage>({"out"}, format);
		AddDescriptorInput<myvk_rg::Usage::kStorageImageR, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT>({1}, {"out"},
		                                                                                           out_image->Alias());
	}
	inline ~ImageRPass() final = default;
	inline myvk::Ptr<myvk::ComputePipeline> CreatePipeline() const final { return nullptr; }
	inline void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const final {}
	inline auto GetImageOutput() { return MakeImageOutput({"out"}); }
};

class ImageWPass final : public myvk_rg::ComputePassBase {
public:
	inline ImageWPass(myvk_rg::Parent parent, const myvk_rg::Image &image) : myvk_rg::ComputePassBase(parent) {
		AddDescriptorInput<myvk_rg::Usage::kStorageImageRW, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT>({0}, {"out"},
		                                                                                            image);
	}
	inline ~ImageWPass() final = default;
	inline myvk::Ptr<myvk::ComputePipeline> CreatePipeline() const final { return nullptr; }
	inline void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const final {}
	inline auto GetImageOutput() { return MakeImageOutput({"out"}); }
};
class BufferWPass final : public myvk_rg::ComputePassBase {
public:
	inline BufferWPass(myvk_rg::Parent parent, const myvk_rg::Buffer &buffer) : myvk_rg::ComputePassBase(parent) {
		AddDescriptorInput<myvk_rg::Usage::kStorageBufferRW, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT>({0}, {"out"},
		                                                                                             buffer);
	}
	inline ~BufferWPass() final = default;
	inline myvk::Ptr<myvk::ComputePipeline> CreatePipeline() const final { return nullptr; }
	inline void CmdExecute(const myvk::Ptr<myvk::CommandBuffer> &command_buffer) const final {}
	inline auto GetBufferOutput() { return MakeBufferOutput({"out"}); }
};

class MyRenderGraph2 final : public myvk_rg::RenderGraphBase {
public:
	inline MyRenderGraph2() : myvk_rg::RenderGraphBase(nullptr) {
		auto format = VK_FORMAT_R32G32B32A32_SFLOAT;

		auto lf_image = CreateResource<myvk_rg::ManagedImage>({"lf"}, format);
		auto src_pass = CreatePass<InputAttPass>({"src"}, lf_image->Alias(), format);

		auto buffer0 = CreateResource<myvk_rg::ManagedBuffer>({"b", 0});
		buffer0->SetSize(100);
		auto buffer1 = CreateResource<myvk_rg::ManagedBuffer>({"b", 1});
		buffer1->SetSize(111);

		auto wb0_pass = CreatePass<BufferWPass>({"wb", 0}, buffer0->Alias());
		auto wb1_pass = CreatePass<BufferWPass>({"wb", 1}, buffer1->Alias());

		auto cbuffer = CreateResource<myvk_rg::CombinedBuffer>(
		    {"cb"}, std::vector{wb0_pass->GetBufferOutput(), wb1_pass->GetBufferOutput()});
		auto wcb_pass = CreatePass<BufferWPass>({"wcb"}, cbuffer->Alias());

		auto read1_pass = CreatePass<InputAttPass>({"read", 1}, src_pass->GetImageOutput(), format);
		auto read2_pass = CreatePass<InputAttPass>({"read", 2}, src_pass->GetImageOutput(), format);
		auto read3_pass = CreatePass<ImageRPass>({"read", 3}, src_pass->GetImageOutput(), format);
		auto read4_pass = CreatePass<SamplerPass>({"read", 4}, src_pass->GetImageOutput(), format);
		auto read5_pass = CreatePass<SamplerPass>({"read", 5}, src_pass->GetImageOutput(), format);

		auto combined_image = CreateResource<myvk_rg::CombinedImage>(
		    {"combined"}, VK_IMAGE_VIEW_TYPE_2D_ARRAY,
		    std::vector{src_pass->GetImageOutput(), read1_pass->GetImageOutput(), read2_pass->GetImageOutput(),
		                read3_pass->GetImageOutput(), read4_pass->GetImageOutput(), read5_pass->GetImageOutput()});

		auto write_pass = CreatePass<ImageWPass>({"write"}, combined_image->Alias());

		AddResult({"final"}, write_pass->GetImageOutput());
		AddResult({"f2"}, wcb_pass->GetBufferOutput());

		SetCanvasSize({1280, 720});
	}
	inline ~MyRenderGraph2() final = default;
};

#include "../../src/rg/executor/default/Collection.hpp"
#include "../../src/rg/executor/default/Dependency.hpp"
#include "../../src/rg/executor/default/Metadata.hpp"
#include "../../src/rg/executor/default/Schedule.hpp"
TEST_SUITE("Default Executor") {
	auto render_graph = myvk::MakePtr<MyRenderGraph2>();

	using myvk_rg_executor::Collection;
	using myvk_rg_executor::Dependency;
	using myvk_rg_executor::Metadata;
	using myvk_rg_executor::Schedule;

	using myvk_rg::interface::PassBase;
	using myvk_rg::interface::ResourceBase;

	using myvk_rg::interface::overloaded;

	Collection collection;
	TEST_CASE("Test Collection") {
		collection = Collection::Create(*render_graph);
		/* printf("PASSES:\n");
		for (const auto &it : collection.GetPasses())
		    printf("%s\n", it.first.Format().c_str());
		printf("INPUTS:\n");
		for (const auto &it : collection.GetInputs()) {
		    printf("%s -> %d, %s\n", it.first.Format().c_str(), static_cast<int>(it.second->GetInputAlias().GetState()),
		           it.second->GetInputAlias().GetSourceKey().Format().c_str());
		}
		printf("RESOURCES:\n");
		for (const auto &it : collection.GetResources())
		    printf("%s\n", it.first.Format().c_str()); */
	}

	Dependency dependency;
	TEST_CASE("Test Dependency") {
		dependency = Dependency::Create({.render_graph = *render_graph, .collection = collection});

		dependency.GetPassGraph().WriteGraphViz(
		    std::cout,
		    [](const PassBase *p_pass) {
			    return p_pass
			               ? p_pass->GetGlobalKey().Format() + ";" + std::to_string(Dependency::GetPassTopoID(p_pass))
			               : "start";
		    },
		    [](const Dependency::PassEdge &e) {
			    return e.p_dst_input->GetGlobalKey().Format() + ";" +
			           std::to_string(Dependency::GetResourceRootID(e.p_resource)) + ":" +
			           std::to_string(static_cast<int>(e.type));
		    });

		dependency.GetResourceGraph().WriteGraphViz(
		    std::cout,
		    [](const ResourceBase *p_resource) {
			    return p_resource->GetGlobalKey().Format() + ";" +
			           std::to_string(Dependency::GetResourceRootID(p_resource));
		    },
		    [](const Dependency::ResourceEdge &e) { return "SUB"; });

		{
			printf("Pass Less:\n");
			for (std::size_t i = 0; i < dependency.GetPassCount(); ++i) {
				for (std::size_t j = 0; j < dependency.GetPassCount(); ++j)
					printf(dependency.IsPassLess(i, j) ? "1" : "0");
				printf("\n");
			}
		}
		{
			printf("Resource Less:\n");
			for (std::size_t i = 0; i < dependency.GetRootResourceCount(); ++i) {
				for (std::size_t j = 0; j < dependency.GetRootResourceCount(); ++j)
					printf(dependency.IsResourceLess(i, j) ? "1" : "0");
				printf("\n");
			}
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

	Metadata metadata;
	TEST_CASE("Test Metadata") {
		metadata =
		    Metadata::Create({.render_graph = *render_graph, .collection = collection, .dependency = dependency});

		printf("ALLOC:\n");
		for (const ResourceBase *p_resource : dependency.GetRootResources()) {
			printf("%s, ", p_resource->GetGlobalKey().Format().c_str());
			p_resource->Visit(overloaded(
			    [](const myvk_rg::interface::ImageBase *p_image) {
				    const auto &view = Metadata::GetViewInfo(p_image);
				    printf("size={%dx%dx%d, mips=%d, layers=%d}", view.size.GetExtent().width,
				           view.size.GetExtent().height, view.size.GetExtent().depth, view.size.GetMipLevels(),
				           view.size.GetArrayLayers());
			    },
			    [](const myvk_rg::interface::BufferBase *p_buffer) {
				    const auto &view = Metadata::GetViewInfo(p_buffer);
				    printf("offset=%lu, size=%lu", view.offset, view.size);
			    }));
			printf("\n");
		}
		printf("View:\n");
		for (const ResourceBase *p_resource : dependency.GetResources()) {
			printf("%s, ", p_resource->GetGlobalKey().Format().c_str());
			p_resource->Visit(overloaded(
			    [](const myvk_rg::interface::ImageBase *p_image) {
				    const auto &view = Metadata::GetViewInfo(p_image);
				    printf("size={%dx%dx%d, mips=%d, layers=%d}, base_layer=%d", view.size.GetExtent().width,
				           view.size.GetExtent().height, view.size.GetExtent().depth, view.size.GetMipLevels(),
				           view.size.GetArrayLayers(), view.base_layer);
			    },
			    [](const myvk_rg::interface::BufferBase *p_buffer) {
				    const auto &view = Metadata::GetViewInfo(p_buffer);
				    printf("offset=%lu, size=%lu", view.offset, view.size);
			    }));
			printf("\n");
		}
	}

	Schedule schedule;
	TEST_CASE("Test Schedule") {
		schedule = Schedule::Create({
		    .render_graph = *render_graph,
		    .collection = collection,
		    .dependency = dependency,
		    .metadata = metadata,
		});

		printf("Pass Groups:\n");
		for (const auto &pass_group : schedule.GetPassGroups()) {
			printf("Pass Group #%zu: ", Schedule::GetGroupID(pass_group.subpasses[0]));
			for (const PassBase *p_subpass : pass_group.subpasses) {
				printf("%s:%zu, ", p_subpass->GetGlobalKey().Format().c_str(), Schedule::GetSubpassID(p_subpass));
			}
			printf("\n");
		}

		printf("Barriers:\n");
		for (const auto &pass_barrier : schedule.GetPassBarriers()) {
			printf("resource = %s\nfrom=", pass_barrier.p_resource->GetGlobalKey().Format().c_str());
			for (const auto &p_src : pass_barrier.src_s) {
				printf("%s:use=%d; ", p_src->GetGlobalKey().Format().c_str(), static_cast<int>(p_src->GetUsage()));
			}
			printf("\nto=");
			for (const auto &p_dst : pass_barrier.dst_s) {
				printf("%s:use=%d; ", p_dst->GetGlobalKey().Format().c_str(), static_cast<int>(p_dst->GetUsage()));
			}
			printf("\ntype=%d\n\n", static_cast<int>(pass_barrier.type));
		}

		printf("Validate Accesses:\n");
		for (const auto *p_resource : dependency.GetResourceGraph().GetVertices()) {
			printf("resource=%s: ", p_resource->GetGlobalKey().Format().c_str());
			CHECK_FALSE(Schedule::GetLastInputs(p_resource).empty());
			for (const auto *p_access : Schedule::GetFirstInputs(p_resource))
				printf("%s, ", p_access->GetGlobalKey().Format().c_str());
			printf("\n");
		}
		printf("\n");

		printf("Last Accesses:\n");
		for (const auto *p_resource : dependency.GetRootResources()) {
			printf("resource=%s: ", p_resource->GetGlobalKey().Format().c_str());
			CHECK_FALSE(Schedule::GetLastInputs(p_resource).empty());
			for (const auto *p_access : Schedule::GetLastInputs(p_resource))
				printf("%s, ", p_access->GetGlobalKey().Format().c_str());
			printf("\n");
		}
	}
}