//
// Created by adamyuan on 2/8/24.
//

#include "Allocation.hpp"

namespace default_executor {

CompileResult<Allocation> Allocation::Create(const myvk::Ptr<myvk::Device> &device_ptr, const Args &args) {
	Allocation alloc = {};
	alloc.m_device_ptr = device_ptr;
	UNWRAP(alloc.fetch_alloc_sizes(args));
	return alloc;
}

CompileResult<void> Allocation::fetch_alloc_sizes(const Args &args) {
	const auto get_size = [&](const auto &size_variant) {
		const auto get_size_visitor = overloaded(
		    [&](const std::invocable<VkExtent2D> auto &size_func) {
			    return size_func(args.render_graph.GetCanvasSize());
		    },
		    [](const auto &size) { return size; });

		return std::visit(get_size_visitor, size_variant);
	};

	const auto combine_size = [&](const CombinedImage *p_combined_image) -> CompileResult<void> {
		const auto combine_size_impl = [&](const LocalInternalImage auto *p_image,
		                                   auto &&combine_size) -> CompileResult<void> {
			UNWRAP_ASSIGN( //
			    get_image_alloc(p_image).size,
			    overloaded(
			        // Combined Image
			        [&](const CombinedImage *p_combined_image) -> CompileResult<SubImageSize> {
				        SubImageSize size = {};
				        for (auto [p_sub, _, _1] : args.dependency.GetResourceGraph().GetOutEdges(p_combined_image)) {
					        // Foreach Sub-Image
					        UNWRAP(p_sub->Visit(overloaded(
					            [&](const LocalInternalImage auto *p_sub) -> CompileResult<void> {
						            return combine_size(p_sub, combine_size);
					            },
					            [](auto &&) -> CompileResult<void> { return {}; })));

					        const auto &sub_size = get_image_alloc(p_sub).size;
					        if (!size.Merge(sub_size))
						        return error::ImageNotMerge{.key = p_combined_image->GetGlobalKey()};

					        get_image_alloc(p_sub).base_layer = size.GetArrayLayers() - sub_size.GetArrayLayers();
				        }
				        return size;
			        },
			        // Managed Image
			        [&](const ManagedImage *p_managed_image) -> CompileResult<SubImageSize> {
				        return get_size(p_managed_image->GetSize());
			        })(p_image));
			return {};
		};
		UNWRAP(combine_size_impl(p_combined_image, combine_size_impl));

		const auto accumulate_base_impl = [&](const CombinedImage *p_combined_image, auto &&accumulate_base) -> void {
			for (auto [p_sub, _, _1] : args.dependency.GetResourceGraph().GetOutEdges(p_combined_image)) {
				get_image_alloc(p_sub).base_layer += get_image_alloc(p_combined_image).base_layer;

				p_sub->Visit(overloaded([&](const CombinedImage *p_sub) { accumulate_base(p_sub, accumulate_base); },
				                        [](auto &&) {}));
			}
		};
		accumulate_base_impl(p_combined_image, accumulate_base_impl);

		return {};
	};

	for (const ResourceBase *p_resource : args.dependency.GetPhysIDResources()) {
		p_resource->Visit(overloaded(
		    [&](const CombinedImage *p_combined_image) -> CompileResult<void> {
			    return combine_size(p_combined_image);
		    },
		    [&](const ManagedImage *p_managed_image) -> CompileResult<void> {
			    get_image_alloc(p_managed_image).size = get_size(p_managed_image->GetSize());
			    return {};
		    },
		    [&](const ManagedBuffer *p_managed_buffer) -> CompileResult<void> {
			    get_buffer_alloc(p_managed_buffer).size = get_size(p_managed_buffer->GetSize());
			    return {};
		    },
		    [](auto &&) -> CompileResult<void> { return {}; }));
	}

	return {};
}

} // namespace default_executor
