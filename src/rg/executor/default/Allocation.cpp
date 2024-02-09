//
// Created by adamyuan on 2/8/24.
//

#include "Allocation.hpp"

namespace default_executor {

Allocation Allocation::Create(const myvk::Ptr<myvk::Device> &device_ptr, const Args &args) {
	Allocation alloc = {};
	alloc.m_device_ptr = device_ptr;
	alloc.fetch_alloc_sizes(args);
	return alloc;
}

void Allocation::fetch_alloc_sizes(const Args &args) {
	const auto get_size = [&](const auto &size_variant) {
		const auto get_size_visitor = overloaded(
		    [&](const std::invocable<VkExtent2D> auto &size_func) {
			    return size_func(args.render_graph.GetCanvasSize());
		    },
		    [](const auto &size) { return size; });

		return std::visit(get_size_visitor, size_variant);
	};

	const auto combine_size = [&](const CombinedImage *p_combined_image) {
		const auto combine_size_impl = [&](const LocalInternalImage auto *p_image, auto &&combine_size) {
			get_alloc(p_image).image.size = overloaded(
			    // Combined Image
			    [&](const CombinedImage *p_combined_image) -> SubImageSize {
				    SubImageSize size = {};
				    for (auto [p_sub, _, _1] : args.dependency.GetResourceGraph().GetOutEdges(p_combined_image)) {
					    // Foreach Sub-Image
					    p_sub->Visit(
					        overloaded([&](const LocalInternalImage auto *p_sub) { combine_size(p_sub, combine_size); },
					                   [](auto &&) {}));

					    const auto &sub_size = get_alloc(p_sub).image.size;
					    if (!size.Merge(sub_size))
						    Throw(error::ImageNotMerge{.key = p_combined_image->GetGlobalKey()});

					    // Base Layer (Offset)
					    get_alloc(p_sub).image.base_layer = size.GetArrayLayers() - sub_size.GetArrayLayers();
				    }
				    return size;
			    },
			    // Managed Image
			    [&](const ManagedImage *p_managed_image) -> SubImageSize {
				    return get_size(p_managed_image->GetSize());
			    })(p_image);
		};
		combine_size_impl(p_combined_image, combine_size_impl);

		// Accumulate Base Layer Offsets
		const auto accumulate_base_impl = [&](const CombinedImage *p_combined_image, auto &&accumulate_base) -> void {
			for (auto [p_sub, _, _1] : args.dependency.GetResourceGraph().GetOutEdges(p_combined_image)) {
				get_alloc(p_sub).image.base_layer += get_alloc(p_combined_image).image.base_layer;

				p_sub->Visit(overloaded([&](const CombinedImage *p_sub) { accumulate_base(p_sub, accumulate_base); },
				                        [](auto &&) {}));
			}
		};
		accumulate_base_impl(p_combined_image, accumulate_base_impl);
	};

	for (const ResourceBase *p_resource : args.dependency.GetPhysIDResources()) {
		// Collect Size
		p_resource->Visit(overloaded([&](const CombinedImage *p_combined_image) { combine_size(p_combined_image); },
		                             [&](const ManagedImage *p_managed_image) {
			                             get_alloc(p_managed_image).image.size = get_size(p_managed_image->GetSize());
		                             },
		                             [&](const ManagedBuffer *p_managed_buffer) {
			                             get_alloc(p_managed_buffer).buffer.size =
			                                 get_size(p_managed_buffer->GetSize());
		                             },
		                             [](auto &&) {}));

		// Set flags
		p_resource->Visit(overloaded(
		    [&](const LocalInternalResource auto *p_resource) {
			    // Double Buffer if LastFrame Resource >= Resource
			    const ResourceBase *p_lf_resource = Dependency::GetLFResource(p_resource);
			    get_alloc(p_resource).double_buffer =
			        p_lf_resource && !args.dependency.IsResourceLess(p_lf_resource, p_resource);
			    // Only Local Internal Resource should be allocated
			    get_alloc(p_resource).should_alloc = true;
		    },
		    [](auto &&) {}));
	}
}

} // namespace default_executor
