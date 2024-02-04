#include "Dependency.hpp"

CompileResult<Dependency> Dependency::Create(const Args &args) {
	Dependency g = {};

	for (const auto &it : args.render_graph.GetResultPoolData()) {
		UNWRAP(it.second.Visit([&](const auto *p_alias) -> CompileResult<void> {
			const PassBase *p_pass;
			UNWRAP_ASSIGN(p_pass, args.collection.FindPass(p_alias->GetSourcePassKey()));
			UNWRAP(g.traverse_pass(args, p_pass));
			return {};
		}));
	}
	return g;
}

CompileResult<void> Dependency::traverse_pass(const Args &args, const PassBase *p_pass) {
	if (m_pass_graph.HasVertex(p_pass))
		return {};
	const auto pass_visitor = overloaded(
	    [&](const PassWithInput auto *p_pass) -> CompileResult<void> {
		    for (const auto &it : p_pass->GetInputPoolData()) {
			    const InputBase *p_input = it.second.template Get<InputBase>();

			    UNWRAP(p_input->GetInputAlias().Visit(overloaded(
			        [&](const OutputAlias auto *p_output_alias) -> CompileResult<void> {
				        const PassBase *p_src_pass;
				        UNWRAP_ASSIGN(p_src_pass, args.collection.FindPass(p_output_alias->GetSourcePassKey()));
				        const InputBase *p_src_input;
				        UNWRAP_ASSIGN(p_src_input, args.collection.FindInput(p_output_alias->GetSourceKey()));

				        UNWRAP(traverse_pass(args, p_src_pass));
				        const ResourceBase *p_resource = m_input_2_resource.at(p_src_input);
				        m_input_2_resource[p_input] = p_resource;

				        m_pass_graph.AddEdge(p_src_pass, p_pass,
				                             PassEdge{p_src_input, p_input, p_resource, EdgeType::kLocal});

				        return {};
			        },
			        [&](const RawAlias auto *p_raw_alias) -> CompileResult<void> {
				        const ResourceBase *p_resource;
				        UNWRAP_ASSIGN(p_resource, args.collection.FindResource(p_raw_alias->GetSourceKey()));
				        m_resource_graph.AddVertex(p_resource);
				        m_input_2_resource[p_input] = p_resource;

				        UNWRAP(p_resource->Visit(overloaded(
				            [&](const CombinedImage *p_combined_image) -> CompileResult<void> {
					            for (const OutputImageAlias &src_alias : p_combined_image->GetSubImages()) {
						            const PassBase *p_src_pass;
						            UNWRAP_ASSIGN(p_src_pass, args.collection.FindPass(src_alias.GetSourcePassKey()));
						            const InputBase *p_src_input;
						            UNWRAP_ASSIGN(p_src_input, args.collection.FindInput(src_alias.GetSourceKey()));

						            UNWRAP(traverse_pass(args, p_src_pass));
						            const ResourceBase *p_sub_image = m_input_2_resource.at(p_src_input);

						            m_pass_graph.AddEdge(p_src_pass, p_pass,
						                                 PassEdge{p_src_input, p_input, p_sub_image, EdgeType::kLocal});
						            m_resource_graph.AddEdge(p_resource, p_sub_image, {EdgeType::kLocal});
					            }
					            return {};
				            },
				            [&](const LastFrameResource auto *p_lf_resource) -> CompileResult<void> {
					            m_pass_graph.AddEdge(nullptr, p_pass,
					                                 PassEdge{nullptr, p_input, p_resource, EdgeType::kLocal});

					            const auto &src_alias = p_lf_resource->GetPointedAlias();
					            const PassBase *p_src_pass;
					            UNWRAP_ASSIGN(p_src_pass, args.collection.FindPass(src_alias.GetSourcePassKey()));
					            const InputBase *p_src_input;
					            UNWRAP_ASSIGN(p_src_input, args.collection.FindInput(src_alias.GetSourceKey()));

					            UNWRAP(traverse_pass(args, p_src_pass));
					            const ResourceBase *p_src_resource = m_input_2_resource.at(p_src_input);

					            // Last Frame Edges
					            m_pass_graph.AddEdge(
					                p_src_pass, p_pass,
					                PassEdge{p_src_input, p_input, p_src_resource, EdgeType::kLastFrame});
					            m_resource_graph.AddEdge(p_resource, p_src_resource, {EdgeType::kLastFrame});
					            return {};
				            },
				            [&](auto &&) -> CompileResult<void> {
					            m_pass_graph.AddEdge(nullptr, p_pass,
					                                 PassEdge{nullptr, p_input, p_resource, EdgeType::kLocal});
					            return {};
				            })));

				        return {};
			        })));
		    }
		    return {};
	    },
	    [](const auto *) -> CompileResult<void> { return {}; });

	UNWRAP(p_pass->Visit(pass_visitor));
	return {};
}
