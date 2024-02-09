//
// Created by adamyuan on 2/8/24.
//

#include "Allocation.hpp"

#include "../VkHelper.hpp"
#include "Info.hpp"

namespace default_executor {

class RenderGraphImage final : public myvk::ImageBase {
private:
	myvk::Ptr<myvk::Device> m_device_ptr;

public:
	inline RenderGraphImage(const myvk::Ptr<myvk::Device> &device, const VkImageCreateInfo &create_info)
	    : m_device_ptr{device} {
		vkCreateImage(GetDevicePtr()->GetHandle(), &create_info, nullptr, &m_image);
		m_extent = create_info.extent;
		m_mip_levels = create_info.mipLevels;
		m_array_layers = create_info.arrayLayers;
		m_format = create_info.format;
		m_type = create_info.imageType;
		m_usage = create_info.usage;
	}
	inline ~RenderGraphImage() final {
		if (m_image != VK_NULL_HANDLE)
			vkDestroyImage(GetDevicePtr()->GetHandle(), m_image, nullptr);
	};
	const myvk::Ptr<myvk::Device> &GetDevicePtr() const final { return m_device_ptr; }
};
class RenderGraphBuffer final : public myvk::BufferBase {
private:
	myvk::Ptr<myvk::Device> m_device_ptr;

public:
	inline RenderGraphBuffer(const myvk::Ptr<myvk::Device> &device, const VkBufferCreateInfo &create_info)
	    : m_device_ptr{device} {
		vkCreateBuffer(GetDevicePtr()->GetHandle(), &create_info, nullptr, &m_buffer);
		m_size = create_info.size;
	}
	inline ~RenderGraphBuffer() final {
		if (m_buffer != VK_NULL_HANDLE)
			vkDestroyBuffer(GetDevicePtr()->GetHandle(), m_buffer, nullptr);
	}
	const myvk::Ptr<myvk::Device> &GetDevicePtr() const final { return m_device_ptr; }
};

Allocation Allocation::Create(const myvk::Ptr<myvk::Device> &device_ptr, const Args &args) {
	Allocation alloc = {};
	alloc.m_device_ptr = device_ptr;
	alloc.tag_alloc_resources(args);

	alloc.fetch_alloc_sizes(args);
	alloc.fetch_alloc_usages(args);

	alloc.create_vk_resources();
	alloc.create_vk_image_views();

	return alloc;
}

void Allocation::tag_alloc_resources(const Args &args) {
	for (const ResourceBase *p_resource : args.dependency.GetResourceGraph().GetVertices()) {
		// Skip External Resources
		if (p_resource->GetState() == ResourceState::kExternal) {
			get_alloc_info(p_resource).p_alloc_resource = nullptr;
			get_alloc_info(p_resource).p_view_resource = nullptr;
			// get_alloc_info(p_resource).alloc_id = -1;
			continue;
		}

		auto p_alloc = Dependency::GetRootResource(p_resource), p_view = p_resource;
		if (p_resource->GetState() == ResourceState::kLastFrame)
			p_alloc = p_view = Dependency::GetLFResource(p_alloc);

		auto &alloc_info = get_alloc_info(p_resource);

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
			get_alloc_info(p_resource).alloc_id = get_alloc_info(GetAllocResource(p_resource)).alloc_id;
		if (!IsViewResource(p_resource) && GetViewResource(p_resource))
			get_alloc_info(p_resource).view_id = get_alloc_info(GetViewResource(p_resource)).view_id;
	}
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

	const auto combine_size = [&](const LocalInternalImage auto *p_image) {
		const auto combine_size_impl = [&](const LocalInternalImage auto *p_image, auto &&combine_size) {
			auto &image_alloc = get_alloc_info(p_image).image;
			image_alloc.vk_view_type = p_image->GetViewType();
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

					    const auto &sub_size = get_alloc_info(p_sub).image.size;
					    if (!size.Merge(sub_size))
						    Throw(error::ImageNotMerge{.key = p_combined_image->GetGlobalKey()});

					    VkFormat sub_format = get_alloc_info(p_sub).image.vk_format;
					    if (format != VK_FORMAT_UNDEFINED && format != sub_format)
						    Throw(error::ImageNotMerge{.key = p_combined_image->GetGlobalKey()});
					    format = sub_format;

					    // Base Layer (Offset)
					    get_alloc_info(p_sub).image.base_layer = size.GetArrayLayers() - sub_size.GetArrayLayers();
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
		if (get_alloc_info(p_image).image.size.GetBaseMipLevel() != 0)
			Throw(error::ImageNotMerge{.key = p_image->GetGlobalKey()});

		// Accumulate Base Layer Offsets
		const auto accumulate_base_impl = overloaded(
		    [&](const CombinedImage *p_combined_image, auto &&accumulate_base) -> void {
			    for (auto [p_sub, _, _1] : args.dependency.GetResourceGraph().GetOutEdges(p_combined_image)) {
				    get_alloc_info(p_sub).image.base_layer += get_alloc_info(p_combined_image).image.base_layer;

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
			                             get_alloc_info(p_managed_buffer).buffer.size =
			                                 get_size(p_managed_buffer->GetSize());
		                             },
		                             [](auto &&) {}));
		// Check Double Buffer
		p_resource->Visit(overloaded(
		    [&](const LocalInternalResource auto *p_resource) {
			    // Double Buffer if LastFrame Resource >= Resource
			    const ResourceBase *p_lf_resource = Dependency::GetLFResource(p_resource);
			    get_alloc_info(p_resource).double_buffer =
			        p_lf_resource && !args.dependency.IsResourceLess(p_lf_resource, p_resource);
		    },
		    [](auto &&) {}));
	}
}

void Allocation::fetch_alloc_usages(const Allocation::Args &args) {
	for (auto [_, p_pass, e, _1] : args.dependency.GetPassGraph().GetEdges()) {
		e.p_resource->Visit(overloaded(
		    [&](const ImageBase *p_image) {
			    get_alloc_info(GetAllocResource(p_image)).image.vk_usages |=
			        UsageGetCreationUsages(e.p_dst_input->GetUsage());
		    },
		    [&](const BufferBase *p_buffer) {
			    get_alloc_info(GetAllocResource(p_buffer)).buffer.vk_usages |=
			        UsageGetCreationUsages(e.p_dst_input->GetUsage());
		    }));

		e.p_resource->Visit(overloaded(
		    [&](const LastFrameImage *p_lf_image) {
			    if (p_lf_image->GetInitTransferFunc())
				    get_alloc_info(GetAllocResource(p_lf_image)).image.vk_usages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		    },
		    [&](const LastFrameBuffer *p_lf_buffer) {
			    if (p_lf_buffer->GetInitTransferFunc())
				    get_alloc_info(GetAllocResource(p_lf_buffer)).buffer.vk_usages |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		    },
		    [](auto &&) {}));
	}
}

void Allocation::create_vk_resources() {
	const auto create_image = [&](const ImageBase *p_image) {
		auto &alloc = get_alloc_info(p_image);

		VkImageCreateInfo create_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
		create_info.usage = alloc.image.vk_usages;
		// if (image_info.is_transient)
		//     create_info.usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
		create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		create_info.format = alloc.image.vk_format;
		create_info.samples = VK_SAMPLE_COUNT_1_BIT;
		create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
		create_info.imageType = alloc.image.vk_type;
		{ // Set Size Info
			VkExtent3D &extent = create_info.extent;
			extent = {1, 1, 1};

			const SubImageSize &size = alloc.image.size;
			switch (create_info.imageType) {
			case VK_IMAGE_TYPE_1D: {
				extent.width = size.GetExtent().width;
				create_info.mipLevels = size.GetMipLevels();
				create_info.arrayLayers = size.GetArrayLayers();
			} break;
			case VK_IMAGE_TYPE_2D: {
				extent.width = size.GetExtent().width;
				extent.height = size.GetExtent().height;
				create_info.mipLevels = size.GetMipLevels();
				create_info.arrayLayers = size.GetArrayLayers();
			} break;
			case VK_IMAGE_TYPE_3D: {
				extent.width = size.GetExtent().width;
				extent.height = size.GetExtent().height;
				extent.depth = std::max(size.GetExtent().depth, size.GetArrayLayers());
				create_info.mipLevels = size.GetMipLevels();
				create_info.arrayLayers = 1;
			} break;
			default:;
			}
		}

		alloc.image.myvk_images[0] = std::make_shared<RenderGraphImage>(m_device_ptr, create_info);
		vkGetImageMemoryRequirements(m_device_ptr->GetHandle(), alloc.image.myvk_images[0]->GetHandle(),
		                             &alloc.vk_mem_reqs);

		alloc.image.myvk_images[1] = alloc.double_buffer ? std::make_shared<RenderGraphImage>(m_device_ptr, create_info)
		                                                 : alloc.image.myvk_images[0];
	};
	const auto create_buffer = [&](const BufferBase *p_buffer) {
		auto &alloc = get_alloc_info(p_buffer);
		VkBufferCreateInfo create_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
		create_info.usage = alloc.buffer.vk_usages;
		create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		create_info.size = alloc.buffer.size;

		alloc.buffer.myvk_buffers[0] = std::make_shared<RenderGraphBuffer>(m_device_ptr, create_info);
		vkGetBufferMemoryRequirements(m_device_ptr->GetHandle(), alloc.buffer.myvk_buffers[0]->GetHandle(),
		                              &alloc.vk_mem_reqs);

		alloc.buffer.myvk_buffers[1] = alloc.double_buffer
		                                   ? std::make_shared<RenderGraphBuffer>(m_device_ptr, create_info)
		                                   : alloc.buffer.myvk_buffers[0];
	};

	for (const ResourceBase *p_resource : m_alloc_id_resources)
		p_resource->Visit(overloaded(create_image, create_buffer));
}

void Allocation::create_vk_image_views() {
	const auto create_image_view = [&](const ImageBase *p_image) {
		auto &image_alloc = get_alloc_info(p_image).image;
		auto &root_image_alloc = get_alloc_info(GetAllocResource(p_image)).image;

		VkImageViewCreateInfo create_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
		create_info.format = image_alloc.vk_format;
		create_info.viewType = image_alloc.vk_view_type;
		create_info.subresourceRange.baseArrayLayer = image_alloc.base_layer;
		create_info.subresourceRange.layerCount = image_alloc.size.GetArrayLayers();
		create_info.subresourceRange.baseMipLevel = image_alloc.size.GetBaseMipLevel();
		create_info.subresourceRange.levelCount = image_alloc.size.GetMipLevels();
		create_info.subresourceRange.aspectMask = VkImageAspectFlagsFromVkFormat(image_alloc.vk_format);

		image_alloc.myvk_image_views[0] = myvk::ImageView::Create(root_image_alloc.myvk_images[0], create_info);
		image_alloc.myvk_image_views[1] = root_image_alloc.myvk_images[1] != root_image_alloc.myvk_images[0]
		                                      ? myvk::ImageView::Create(root_image_alloc.myvk_images[1], create_info)
		                                      : image_alloc.myvk_image_views[0];
	};
	for (const ResourceBase *p_resource : m_view_id_resources)
		p_resource->Visit(overloaded(create_image_view, [](auto &&) {}));
}

} // namespace default_executor
