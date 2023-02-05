#include "RenderGraphExecutor.hpp"

#include "VkHelper.hpp"

#include <iostream>
#include <list>
#include <map>

namespace myvk_rg::_details_ {

struct RenderGraphExecutor::SubpassDependencies {
	struct AttachmentDependency {
		VkImageLayout initial_layout{VK_IMAGE_LAYOUT_UNDEFINED}, final_layout{VK_IMAGE_LAYOUT_UNDEFINED};
		bool may_alias{false};
		inline void set_initial_layout(VkImageLayout layout) {
			assert(initial_layout == VK_IMAGE_LAYOUT_UNDEFINED || initial_layout == layout);
			initial_layout = layout;
		}
		inline void set_final_layout(VkImageLayout layout) {
			assert(final_layout == VK_IMAGE_LAYOUT_UNDEFINED || final_layout == layout);
			final_layout = layout;
		}
	};
	const std::vector<RenderGraphScheduler::SubpassDependency> *p_subpass_dependencies{};
	std::vector<RenderGraphScheduler::SubpassDependency> extra_subpass_dependencies;
	std::vector<RenderGraphScheduler::SubpassDependency> validation_subpass_dependencies;
	std::vector<AttachmentDependency> attachment_dependencies;
};

void RenderGraphExecutor::reset_pass_executor_vector() {
	m_pass_executors.clear();
	m_pass_executors.resize(m_p_scheduled->GetPassCount());
	for (uint32_t pass_id = 0; pass_id < m_p_scheduled->GetPassCount(); ++pass_id)
		m_pass_executors[pass_id].p_info = &m_p_scheduled->GetPassInfo(pass_id);

	m_post_barrier_info.clear();
}

std::vector<RenderGraphExecutor::SubpassDependencies> RenderGraphExecutor::extract_barriers_and_subpass_dependencies() {
	std::vector<SubpassDependencies> sub_deps(m_p_scheduled->GetPassCount());
	for (uint32_t i = 0; i < m_p_scheduled->GetPassCount(); ++i) {
		const auto &pass_info = m_p_scheduled->GetPassInfo(i);
		if (pass_info.p_render_pass_info == nullptr)
			continue;
		sub_deps[i].attachment_dependencies.resize(pass_info.p_render_pass_info->attachment_id_map.size());
		sub_deps[i].p_subpass_dependencies = &pass_info.p_render_pass_info->subpass_dependencies;
	}

	// Extract from Pass Dependencies
	for (const auto &dep : m_p_scheduled->GetPassDependencies()) {
		bool is_from_attachment = dep.from.front().p_input && UsageIsAttachment(dep.from.front().p_input->GetUsage());
		bool is_to_attachment = dep.to.front().p_input && UsageIsAttachment(dep.to.front().p_input->GetUsage());

		assert(!is_from_attachment || dep.from.size() == 1);
		assert(!is_to_attachment || dep.to.size() == 1);

		if (!is_from_attachment && !is_to_attachment) {
			// Not Attachment-related, then Add a Vulkan Barrier
			BarrierInfo &barrier_info =
			    dep.to.front().pass ? m_pass_executors[m_p_scheduled->GetPassID(dep.to.front().pass)].prior_barrier_info
			                        : m_post_barrier_info;

			dep.resource->Visit([this, &dep, &barrier_info](const auto *resource) {
				if constexpr (ResourceVisitorTrait<decltype(resource)>::kType == ResourceType::kBuffer) {
					barrier_info.buffer_barriers.emplace_back();
					BufferMemoryBarrier &barrier = barrier_info.buffer_barriers.back();

					barrier.buffer = resource;

					for (auto &link : dep.from) {
						if (link.p_input == nullptr) {
							assert(false);
							continue;
						}
						barrier.src_stage_mask |= link.p_input->GetUsagePipelineStages();
						barrier.src_access_mask |= UsageGetWriteAccessFlags(link.p_input->GetUsage());
					}
					for (auto &link : dep.to) {
						if (link.p_input == nullptr) {
							assert(dep.to.size() == 1);
							// barrier.dstStageMask |= m_
							// TODO: Set DST Stage
							continue;
						}
						barrier.dst_stage_mask |= link.p_input->GetUsagePipelineStages();
						barrier.dst_access_mask |= UsageGetAccessFlags(link.p_input->GetUsage());
					}
				} else {
					barrier_info.image_barriers.emplace_back();
					ImageMemoryBarrier &barrier = barrier_info.image_barriers.back();

					barrier.image = resource;
					for (auto &link : dep.from) {
						if (link.p_input == nullptr) {
							assert(false);
							continue;
						}
						barrier.src_stage_mask |= link.p_input->GetUsagePipelineStages();
						barrier.src_access_mask |= UsageGetWriteAccessFlags(link.p_input->GetUsage());
						assert(barrier.old_layout == VK_IMAGE_LAYOUT_UNDEFINED ||
						       barrier.old_layout == UsageGetImageLayout(link.p_input->GetUsage()));
						barrier.old_layout = UsageGetImageLayout(link.p_input->GetUsage());
					}
					for (auto &link : dep.to) {
						if (link.p_input == nullptr) {
							assert(dep.to.size() == 1);
							// barrier.dstStageMask |= m_
							// TODO: Set DST Stage and Layout
							continue;
						}
						barrier.dst_stage_mask |= link.p_input->GetUsagePipelineStages();
						barrier.dst_access_mask |= UsageGetAccessFlags(link.p_input->GetUsage());
						assert(barrier.new_layout == VK_IMAGE_LAYOUT_UNDEFINED ||
						       barrier.new_layout == UsageGetImageLayout(link.p_input->GetUsage()));
						barrier.new_layout = UsageGetImageLayout(link.p_input->GetUsage());
					}
				}
			});

		} else {
			// Add Extra Vulkan Subpass Dependencies (External) if a Dependency is Attachment-related
			assert(dep.resource->GetType() == ResourceType::kImage);
			auto *image = static_cast<const ImageBase *>(dep.resource);

			if (is_from_attachment && is_to_attachment) {
				const auto &link_from = dep.from.front(), &link_to = dep.to.front();
				assert(link_from.pass || link_to.pass);
				VkImageLayout trans_layout = UsageGetImageLayout(link_from.p_input->GetUsage());
				if (link_from.pass) {
					uint32_t from_pass_id = m_p_scheduled->GetPassID(link_from.pass);
					uint32_t attachment_id =
					    m_p_scheduled->GetPassInfo(from_pass_id).p_render_pass_info->attachment_id_map.at(image);
					sub_deps[from_pass_id].attachment_dependencies[attachment_id].set_final_layout(trans_layout);
				}
				if (link_to.pass) {
					uint32_t to_pass_id = m_p_scheduled->GetPassID(link_to.pass);
					sub_deps[to_pass_id].extra_subpass_dependencies.push_back(
					    {dep.resource, {link_from.p_input, link_from.pass}, {link_to.p_input, link_to.pass}});

					uint32_t attachment_id =
					    m_p_scheduled->GetPassInfo(to_pass_id).p_render_pass_info->attachment_id_map.at(image);
					sub_deps[to_pass_id].attachment_dependencies[attachment_id].set_initial_layout(trans_layout);
				}
			} else if (is_from_attachment) {
				const auto &link_from = dep.from.front();
				if (link_from.pass) {
					uint32_t from_pass_id = m_p_scheduled->GetPassID(link_from.pass);
					uint32_t attachment_id =
					    m_p_scheduled->GetPassInfo(from_pass_id).p_render_pass_info->attachment_id_map.at(image);

					for (const auto &link_to : dep.to) {
						if (link_to.p_input)
							sub_deps[from_pass_id].attachment_dependencies[attachment_id].set_final_layout(
							    UsageGetImageLayout(link_to.p_input->GetUsage()));
						sub_deps[from_pass_id].extra_subpass_dependencies.push_back(
						    {dep.resource, {link_from.p_input, link_from.pass}, {link_to.p_input, link_to.pass}});
					}
				}
			} else {
				assert(is_to_attachment);
				const auto &link_to = dep.to.front();
				if (link_to.pass) {
					uint32_t to_pass_id = m_p_scheduled->GetPassID(link_to.pass);
					uint32_t attachment_id =
					    m_p_scheduled->GetPassInfo(to_pass_id).p_render_pass_info->attachment_id_map.at(image);

					for (const auto &link_from : dep.from) {
						if (link_from.p_input)
							sub_deps[to_pass_id].attachment_dependencies[attachment_id].set_initial_layout(
							    UsageGetImageLayout(link_from.p_input->GetUsage()));
						sub_deps[to_pass_id].extra_subpass_dependencies.push_back(
						    {dep.resource, {link_from.p_input, link_from.pass}, {link_to.p_input, link_to.pass}});
					}
				}
			}
		}
	}

	// Extract from Resource Validation
	// TODO: Src Stages
	for (auto &pass_exec : m_pass_executors) {
		const auto &pass_info = *pass_exec.p_info;

		for (const auto &subpass_info : pass_info.subpasses) {
			const PassBase *pass = subpass_info.pass;
			uint32_t pass_id = m_p_scheduled->GetPassID(pass);
			// uint32_t subpass_id = m_p_scheduled->GetSubpassID(subpass_info.pass);

			if (pass_info.p_render_pass_info) {
				// For RenderPass, insert SubpassDependency
				auto &sub_dep = sub_deps[pass_id];
				for (const auto &res_validate : subpass_info.validate_resources) {
					res_validate.resource->Visit(
					    [this, pass, &sub_dep, &res_validate, &pass_info](const auto *resource) {
						    if constexpr (ResourceVisitorTrait<decltype(resource)>::kType == ResourceType::kImage) {
							    if constexpr (ResourceVisitorTrait<decltype(resource)>::kIsInternal) {
								    uint32_t int_res_id = m_p_resolved->GetIntResourceID(resource);
								    uint32_t att_id = pass_info.p_render_pass_info->attachment_id_map.at(resource);

								    for (uint32_t dep_int_res_id = 0;
								         dep_int_res_id < m_p_resolved->GetIntResourceCount(); ++dep_int_res_id) {
									    // If resource #i and current is aliased, and #i is used prior than current,
									    // then make dependencies
									    if (m_p_allocated->IsIntResourceAliased(dep_int_res_id, int_res_id) &&
									        m_p_resolved->IsIntResourcePrior(dep_int_res_id, int_res_id)) {
										    sub_dep.attachment_dependencies[att_id].may_alias = true;

										    // Insert Non-by-region SubpassDependency
										    for (const auto &last_ref :
										         m_p_resolved->GetIntResourceInfo(dep_int_res_id).last_references) {
											    assert(m_p_resolved->IsPassPrior(last_ref.pass, pass));
											    sub_dep.validation_subpass_dependencies.push_back(
											        {resource,
											         {last_ref.p_input, last_ref.pass},
											         {res_validate.p_input, pass}});
										    }
									    }
								    }
							    } else {
								    // TODO: External Image
							    }
						    } else
							    assert(false);
					    });
				}
			} else {
				// Not RenderPass, then just insert a barrier
				auto &barrier_info = pass_exec.prior_barrier_info;
				for (const auto &res_validate : subpass_info.validate_resources) {
					res_validate.resource->Visit([this, &barrier_info, &res_validate](const auto *resource) {
						if constexpr (ResourceVisitorTrait<decltype(resource)>::kType == ResourceType::kImage) {
							barrier_info.image_barriers.emplace_back();
							auto &barrier = barrier_info.image_barriers.back();
							barrier.image = resource;
							barrier.new_layout = UsageGetImageLayout(res_validate.p_input->GetUsage());
							barrier.dst_access_mask = UsageGetAccessFlags(res_validate.p_input->GetUsage());
							barrier.dst_stage_mask = res_validate.p_input->GetUsagePipelineStages();

							if constexpr (ResourceVisitorTrait<decltype(resource)>::kIsInternal) {
								uint32_t int_res_id = m_p_resolved->GetIntResourceID(resource);
								for (uint32_t dep_int_res_id = 0; dep_int_res_id < m_p_resolved->GetIntResourceCount();
								     ++dep_int_res_id) {
									// If resource #i and current is aliased, and #i is used prior than current, then
									// make dependencies
									if (m_p_allocated->IsIntResourceAliased(dep_int_res_id, int_res_id) &&
									    m_p_resolved->IsIntResourcePrior(dep_int_res_id, int_res_id)) {
										for (const auto &last_ref :
										     m_p_resolved->GetIntResourceInfo(dep_int_res_id).last_references) {
											barrier.src_stage_mask |= last_ref.p_input->GetUsagePipelineStages();
										}
									}
								}
							} else {
								// TODO: External Image
							}
						}
					});
				}
			}
		}
	}

	return sub_deps;
}

struct SubpassDependencyKey {
	uint32_t src_subpass{}, dst_subpass{};
	bool is_by_region{};

	inline bool operator<(const SubpassDependencyKey &r) const {
		return std::tie(src_subpass, dst_subpass, is_by_region) <
		       std::tie(r.src_subpass, r.dst_subpass, r.is_by_region);
	}
};

struct SubpassDescription {
	std::vector<VkAttachmentReference2> input_attachments, color_attachments;
	std::vector<uint32_t> preserve_attachments;
	std::optional<VkAttachmentReference2> depth_attachment;
};

void RenderGraphExecutor::create_render_passes_and_framebuffers(
    std::vector<SubpassDependencies> &&subpass_dependencies) {
	for (uint32_t pass_id = 0; pass_id < m_p_scheduled->GetPassCount(); ++pass_id) {
		auto &pass_exec = m_pass_executors[pass_id];
		const auto &pass_info = *pass_exec.p_info;
		const auto &pass_sub_deps = subpass_dependencies[pass_id];

		if (pass_info.p_render_pass_info == nullptr)
			continue;

		// Subpass Dependencies
		std::map<SubpassDependencyKey, VkMemoryBarrier2> subpass_dependency_map;

		const auto merge_subpass_dependencies = [this, pass_id, &subpass_dependency_map](
		                                            const RenderGraphScheduler::SubpassDependency &sub_dep,
		                                            bool is_by_region) {
			uint32_t src_subpass = sub_dep.from.pass && m_p_scheduled->GetPassID(sub_dep.from.pass) == pass_id
			                           ? m_p_scheduled->GetSubpassID(sub_dep.from.pass)
			                           : VK_SUBPASS_EXTERNAL;
			uint32_t dst_subpass = sub_dep.to.pass && m_p_scheduled->GetPassID(sub_dep.to.pass) == pass_id
			                           ? m_p_scheduled->GetSubpassID(sub_dep.to.pass)
			                           : VK_SUBPASS_EXTERNAL;

			SubpassDependencyKey key{src_subpass, dst_subpass, is_by_region};

			VkMemoryBarrier2 *p_barrier;
			auto it = subpass_dependency_map.find(key);
			if (it != subpass_dependency_map.end())
				p_barrier = &it->second;
			else
				p_barrier = &subpass_dependency_map.insert({key, {VK_STRUCTURE_TYPE_MEMORY_BARRIER_2}}).first->second;

			if (sub_dep.from.p_input) {
				p_barrier->srcStageMask |= sub_dep.from.p_input->GetUsagePipelineStages();
				if (is_by_region) // For Non-by-region (resource validation), from.p_input is not for the actual
				                  // resource
					p_barrier->srcAccessMask |= UsageGetWriteAccessFlags(sub_dep.from.p_input->GetUsage());
			}
			if (sub_dep.to.p_input) {
				p_barrier->dstStageMask |= sub_dep.to.p_input->GetUsagePipelineStages();
				p_barrier->dstAccessMask |= UsageGetAccessFlags(sub_dep.to.p_input->GetUsage());
			}
		};

		for (const auto &sub_dep : *pass_sub_deps.p_subpass_dependencies)
			merge_subpass_dependencies(sub_dep, true);
		for (const auto &sub_dep : pass_sub_deps.extra_subpass_dependencies)
			merge_subpass_dependencies(sub_dep, true);
		for (const auto &sub_dep : pass_sub_deps.validation_subpass_dependencies)
			merge_subpass_dependencies(sub_dep, false); // Validation dependencies should not use BY_REGION_BIT

		std::vector<VkSubpassDependency2> vk_subpass_dependencies;
		vk_subpass_dependencies.reserve(subpass_dependency_map.size());
		for (const auto &it : subpass_dependency_map) {
			vk_subpass_dependencies.push_back({VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2});
			VkSubpassDependency2 &dep = vk_subpass_dependencies.back();

			dep.srcSubpass = it.first.src_subpass;
			dep.dstSubpass = it.first.dst_subpass;
			dep.dependencyFlags = it.first.is_by_region ? VK_DEPENDENCY_BY_REGION_BIT : 0u;
			dep.pNext = &it.second;
		}

		// Initialize Attachment Info
		auto &attachments = pass_exec.render_pass_info.attachments;
		attachments.resize(pass_sub_deps.attachment_dependencies.size());
		for (const auto &it : pass_info.p_render_pass_info->attachment_id_map)
			attachments[it.second].image = it.first;

		// Subpass Description (Also Update Attachment Info)
		std::vector<SubpassDescription> subpass_descriptions(pass_info.subpasses.size());
		for (uint32_t subpass_id = 0; subpass_id < pass_info.subpasses.size(); ++subpass_id) {
			const auto &subpass_info = pass_info.subpasses[subpass_id];
			auto &subpass_desc = subpass_descriptions[subpass_id];

			const auto &get_att_id = [&pass_info](const auto *image) -> uint32_t {
				if constexpr (ResourceVisitorTrait<decltype(image)>::kType == ResourceType::kImage) {
					if constexpr (ResourceVisitorTrait<decltype(image)>::kIsAlias)
						return pass_info.p_render_pass_info->attachment_id_map.at(image->GetPointedResource());
					else
						return pass_info.p_render_pass_info->attachment_id_map.at(image);
				} else {
					assert(false);
					return -1;
				}
			};

			// Input Attachments
			subpass_desc.input_attachments.reserve(subpass_info.pass->m_p_attachment_data->m_input_attachments.size());
			for (const Input *p_input : subpass_info.pass->m_p_attachment_data->m_input_attachments) {
				uint32_t att_id = p_input->GetResource()->Visit(get_att_id);
				attachments[att_id].references.push_back({p_input, subpass_id});

				subpass_desc.input_attachments.push_back({VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2});
				auto &att_ref = subpass_desc.input_attachments.back();
				att_ref.attachment = att_id;
				att_ref.layout = UsageGetImageLayout(p_input->GetUsage());
				att_ref.aspectMask = VkImageAspectFlagsFromVkFormat(
				    attachments[att_id].image->GetFormat()); // TODO: Better InputAttachment AspectMask Handling
			}

			// Color Attachments
			subpass_desc.color_attachments.reserve(subpass_info.pass->m_p_attachment_data->m_color_attachments.size());
			for (const Input *p_input : subpass_info.pass->m_p_attachment_data->m_color_attachments) {
				uint32_t att_id = p_input->GetResource()->Visit(get_att_id);
				attachments[att_id].references.push_back({p_input, subpass_id});

				subpass_desc.color_attachments.push_back({VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2});
				auto &att_ref = subpass_desc.color_attachments.back();
				att_ref.attachment = att_id;
				att_ref.layout = UsageGetImageLayout(p_input->GetUsage());
			}

			// Depth (Stencil) Attachment
			{ // TODO: Support Stencil Attachment
				const Input *p_input = subpass_info.pass->m_p_attachment_data->m_depth_attachment;
				if (p_input) {
					uint32_t att_id = p_input->GetResource()->Visit(get_att_id);
					attachments[att_id].references.push_back({p_input, subpass_id});

					subpass_desc.depth_attachment = {VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2};
					auto &att_ref = subpass_desc.depth_attachment.value();
					att_ref.attachment = att_id;
					att_ref.layout = UsageGetImageLayout(p_input->GetUsage());
				}
			}
		}
		// Add preserve attachments
		for (uint32_t att_id = 0; att_id < attachments.size(); ++att_id) {
			const AttachmentInfo &att_info = attachments[att_id];
			for (uint32_t i = 1; i < att_info.references.size(); ++i) {
				for (uint32_t subpass_id = att_info.references[i - 1].subpass + 1;
				     subpass_id < att_info.references[i].subpass; ++subpass_id) {
					subpass_descriptions[subpass_id].preserve_attachments.push_back(att_id);
				}
			}
		}
		std::vector<VkSubpassDescription2> vk_subpass_descriptions;
		vk_subpass_descriptions.reserve(subpass_descriptions.size());
		for (auto &info : subpass_descriptions) {
			vk_subpass_descriptions.push_back({VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2});
			VkSubpassDescription2 &desc = vk_subpass_descriptions.back();

			desc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

			desc.inputAttachmentCount = info.input_attachments.size();
			desc.pInputAttachments = info.input_attachments.data();

			desc.colorAttachmentCount = info.color_attachments.size();
			desc.pColorAttachments = info.color_attachments.data();
			// TODO: pResolveAttachments

			desc.preserveAttachmentCount = info.preserve_attachments.size();
			desc.pPreserveAttachments = info.preserve_attachments.data();

			desc.pDepthStencilAttachment = info.depth_attachment.has_value() ? &info.depth_attachment.value() : nullptr;
		}

		// Attachment Descriptions
		std::vector<VkAttachmentDescription2> vk_attachment_descriptions;
		vk_attachment_descriptions.reserve(pass_sub_deps.attachment_dependencies.size());
		for (uint32_t att_id = 0; att_id < attachments.size(); ++att_id) {
			const auto &att_dep = pass_sub_deps.attachment_dependencies[att_id];
			const auto &att_info = attachments[att_id];

			vk_attachment_descriptions.push_back({VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2});
			VkAttachmentDescription2 &desc = vk_attachment_descriptions.back();
			desc.flags = att_dep.may_alias ? VK_ATTACHMENT_DESCRIPTION_MAY_ALIAS_BIT : 0u;
			desc.format = att_info.image->GetFormat();
			desc.samples = VK_SAMPLE_COUNT_1_BIT;
			desc.initialLayout = att_dep.initial_layout;
			desc.finalLayout = att_dep.final_layout;

			VkImageAspectFlags aspects = VkImageAspectFlagsFromVkFormat(desc.format);
			VkAttachmentLoadOp initial_load_op = att_info.image->Visit([](const auto *image) -> VkAttachmentLoadOp {
				if constexpr (ResourceVisitorTrait<decltype(image)>::kState == ResourceState::kExternal ||
				              ResourceVisitorTrait<decltype(image)>::kState == ResourceState::kManaged)
					return image->GetLoadOp();
				else {
					assert(false);
					return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				}
			});

			if (aspects & (VK_IMAGE_ASPECT_COLOR_BIT | VK_IMAGE_ASPECT_DEPTH_BIT)) {
				desc.loadOp =
				    desc.initialLayout == VK_IMAGE_LAYOUT_UNDEFINED ? initial_load_op : VK_ATTACHMENT_LOAD_OP_LOAD;
				desc.storeOp = desc.finalLayout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_ATTACHMENT_STORE_OP_DONT_CARE
				                                                             : VK_ATTACHMENT_STORE_OP_STORE;
			}
			if (aspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
				desc.stencilLoadOp =
				    desc.initialLayout == VK_IMAGE_LAYOUT_UNDEFINED ? initial_load_op : VK_ATTACHMENT_LOAD_OP_LOAD;
				desc.stencilStoreOp = desc.finalLayout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_ATTACHMENT_STORE_OP_DONT_CARE
				                                                                    : VK_ATTACHMENT_STORE_OP_STORE;
			}
			// If Attachment is Read-only, use STORE_OP_NONE
			if (att_info.is_read_only()) {
				desc.storeOp = VK_ATTACHMENT_STORE_OP_NONE;
				desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_NONE;
			}

			// UNDEFINED finalLayout is not allowed, just use the last layout as final
			if (desc.finalLayout == VK_IMAGE_LAYOUT_UNDEFINED)
				desc.finalLayout = UsageGetImageLayout(att_info.references.back().p_input->GetUsage());
		}

		// Create RenderPass
		VkRenderPassCreateInfo2 create_info = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2};
		create_info.dependencyCount = vk_subpass_dependencies.size();
		create_info.pDependencies = vk_subpass_dependencies.data();

		create_info.attachmentCount = vk_attachment_descriptions.size();
		create_info.pAttachments = vk_attachment_descriptions.data();

		create_info.subpassCount = vk_subpass_descriptions.size();
		create_info.pSubpasses = vk_subpass_descriptions.data();

		pass_exec.render_pass_info.myvk_render_pass =
		    myvk::RenderPass::Create(m_p_render_graph->GetDevicePtr(), create_info);

		// Create Framebuffer
		RenderGraphScheduler::RenderPassArea area = pass_info.p_render_pass_info->area;

		std::vector<VkFramebufferAttachmentImageInfo> attachment_image_infos;
		attachment_image_infos.reserve(attachments.size());
		std::vector<VkFormat> attachment_image_formats;
		attachment_image_formats.reserve(attachments.size());
		for (const auto &att_info : attachments) {
			attachment_image_infos.push_back({VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO});
			auto &image_info = attachment_image_infos.back();

			attachment_image_formats.push_back(att_info.image->GetFormat());

			image_info.usage = m_p_allocated->GetVkImage(att_info.image)->GetUsage();
			image_info.width = area.extent.width;
			image_info.height = area.extent.height;
			image_info.layerCount = area.layers;
			image_info.viewFormatCount = 1;
			image_info.pViewFormats = &attachment_image_formats.back();
		}
		pass_exec.render_pass_info.myvk_framebuffer = myvk::ImagelessFramebuffer::Create(
		    pass_exec.render_pass_info.myvk_render_pass, attachment_image_infos, area.extent, area.layers);
	}
}

void RenderGraphExecutor::Prepare(const RenderGraphBase *p_render_graph, const RenderGraphResolver &resolved,
                                  const RenderGraphScheduler &scheduled, const RenderGraphAllocator &allocated) {
	m_p_render_graph = p_render_graph;
	m_p_resolved = &resolved;
	m_p_scheduled = &scheduled;
	m_p_allocated = &allocated;

	reset_pass_executor_vector();
	auto subpass_dependencies = extract_barriers_and_subpass_dependencies();
	create_render_passes_and_framebuffers(std::move(subpass_dependencies));
}

} // namespace myvk_rg::_details_
