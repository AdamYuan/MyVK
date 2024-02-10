//
// Created by adamyuan on 2/10/24.
//

#include "ResourceMeta.hpp"

#include "../VkHelper.hpp"

namespace default_executor {

ResourceMeta ResourceMeta::Create(const Args &args) {
	ResourceMeta r = {};
	r.tag_alloc_resources(args);
	r.fetch_alloc_sizes(args);
	r.fetch_alloc_usages(args);
	return r;
}

void ResourceMeta::tag_alloc_resources(const Args &args) {
	for (const ResourceBase *p_resource : args.dependency.GetResourceGraph().GetVertices()) {
		// Skip External Resources
		if (p_resource->GetState() == ResourceState::kExternal) {
			get_meta(p_resource).p_alloc_resource = nullptr;
			get_meta(p_resource).p_view_resource = nullptr;
			// get_meta(p_resource).alloc_id = -1;
			continue;
		}

		auto p_alloc = Dependency::GetRootResource(p_resource), p_view = p_resource;
		if (p_resource->GetState() == ResourceState::kLastFrame)
			p_alloc = p_view = Dependency::GetLFResource(p_alloc);

		auto &alloc_info = get_meta(p_resource);

		alloc_info.p_alloc_resource = p_alloc;
		alloc_info.p_view_resource = p_view;

		if (IsAllocResource(p_resource)) {
			alloc_info.alloc_id = m_alloc_id_resources.size();
			m_alloc_id_resources.push_back(p_resource);
		}
		if (IsViewResource(p_resource)) {
			alloc_info.view_id = m_view_id_resources.size();
			m_view_id_resources.push_back(p_resource);
		}
	}
	for (const ResourceBase *p_resource : args.dependency.GetResourceGraph().GetVertices()) {
		if (!IsAllocResource(p_resource) && GetAllocResource(p_resource))
			get_meta(p_resource).alloc_id = get_meta(GetAllocResource(p_resource)).alloc_id;
		if (!IsViewResource(p_resource) && GetViewResource(p_resource))
			get_meta(p_resource).view_id = get_meta(GetViewResource(p_resource)).view_id;
	}
}

void ResourceMeta::fetch_alloc_sizes(const Args &args) {
	const auto get_size = [&](const auto &size_variant) {
		const auto get_size_visitor = overloaded(
		    [&](const std::invocable<VkExtent2D> auto &size_func) {
			    return size_func(args.render_graph.GetCanvasSize());
		    },
		    [](const auto &size) { return size; });

		return std::visit(get_size_visitor, size_variant);
	};

	const auto combine_size = [&](const LocalInternalImage auto *p_image) {
		const auto combine_size_impl = [&](const LocalInternalImage auto *p_image, auto &&combine_size) {
			auto &image_alloc = get_meta(p_image).image;
			image_alloc.vk_type = VkImageTypeFromVkImageViewType(p_image->GetViewType());
			std::tie(image_alloc.size, image_alloc.vk_format) = overloaded(
			    // Combined Image
			    [&](const CombinedImage *p_combined_image) -> std::tuple<SubImageSize, VkFormat> {
				    SubImageSize size = {};
				    VkFormat format = VK_FORMAT_UNDEFINED;

				    for (auto [p_sub, _, _1] : args.dependency.GetResourceGraph().GetOutEdges(p_combined_image)) {
					    // Foreach Sub-Image
					    p_sub->Visit(
					        overloaded([&](const LocalInternalImage auto *p_sub) { combine_size(p_sub, combine_size); },
					                   [](auto &&) {}));

					    const auto &sub_size = get_meta(p_sub).image.size;
					    if (!size.Merge(sub_size))
						    Throw(error::ImageNotMerge{.key = p_combined_image->GetGlobalKey()});

					    VkFormat sub_format = get_meta(p_sub).image.vk_format;
					    if (format != VK_FORMAT_UNDEFINED && format != sub_format)
						    Throw(error::ImageNotMerge{.key = p_combined_image->GetGlobalKey()});
					    format = sub_format;

					    // Base Layer (Offset)
					    get_meta(p_sub).image.base_layer = size.GetArrayLayers() - sub_size.GetArrayLayers();
				    }
				    return {size, format};
			    },
			    // Managed Image
			    [&](const ManagedImage *p_managed_image) -> std::tuple<SubImageSize, VkFormat> {
				    return {get_size(p_managed_image->GetSize()), p_managed_image->GetFormat()};
			    })(p_image);
		};
		combine_size_impl(p_image, combine_size_impl);

		// Base Mip Level should be 0
		if (get_meta(p_image).image.size.GetBaseMipLevel() != 0)
			Throw(error::ImageNotMerge{.key = p_image->GetGlobalKey()});

		// Accumulate Base Layer Offsets
		const auto accumulate_base_impl = overloaded(
		    [&](const CombinedImage *p_combined_image, auto &&accumulate_base) -> void {
			    for (auto [p_sub, _, _1] : args.dependency.GetResourceGraph().GetOutEdges(p_combined_image)) {
				    get_meta(p_sub).image.base_layer += get_meta(p_combined_image).image.base_layer;

				    p_sub->Visit(overloaded(
				        [&](const CombinedImage *p_sub) { accumulate_base(p_sub, accumulate_base); }, [](auto &&) {}));
			    }
		    },
		    [](auto &&, auto &&) {});
		accumulate_base_impl(p_image, accumulate_base_impl);
	};

	for (const ResourceBase *p_resource : m_alloc_id_resources) {
		// Collect Size
		p_resource->Visit(overloaded([&](const LocalInternalImage auto *p_image) { combine_size(p_image); },
		                             [&](const ManagedBuffer *p_managed_buffer) {
			                             get_meta(p_managed_buffer).buffer.size = get_size(p_managed_buffer->GetSize());
		                             },
		                             [](auto &&) {}));
		// Check Double Buffer
		p_resource->Visit(overloaded(
		    [&](const LocalInternalResource auto *p_resource) {
			    // Double Buffer if LastFrame Resource >= Resource
			    const ResourceBase *p_lf_resource = Dependency::GetLFResource(p_resource);
			    get_meta(p_resource).double_buffer =
			        p_lf_resource && !args.dependency.IsResourceLess(p_lf_resource, p_resource);
		    },
		    [](auto &&) {}));
	}
}

void ResourceMeta::fetch_alloc_usages(const Args &args) {
	for (auto [_, p_pass, e, _1] : args.dependency.GetPassGraph().GetEdges()) {
		e.p_resource->Visit(overloaded(
		    [&](const ImageBase *p_image) {
			    get_meta(GetAllocResource(p_image)).image.vk_usages |=
			        UsageGetCreationUsages(e.p_dst_input->GetUsage());
		    },
		    [&](const BufferBase *p_buffer) {
			    get_meta(GetAllocResource(p_buffer)).buffer.vk_usages |=
			        UsageGetCreationUsages(e.p_dst_input->GetUsage());
		    }));

		e.p_resource->Visit(overloaded(
		    [&](const LastFrameImage *p_lf_image) {
			    if (p_lf_image->GetInitTransferFunc())
				    get_meta(GetAllocResource(p_lf_image)).image.vk_usages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		    },
		    [&](const LastFrameBuffer *p_lf_buffer) {
			    if (p_lf_buffer->GetInitTransferFunc())
				    get_meta(GetAllocResource(p_lf_buffer)).buffer.vk_usages |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		    },
		    [](auto &&) {}));
	}
}

} // namespace default_executor
