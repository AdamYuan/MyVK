#ifndef MYVK_RENDER_GRAPH_HPP
#define MYVK_RENDER_GRAPH_HPP

#include "CommandBuffer.hpp"
#include "FrameManager.hpp"
#include "RenderGraphScheduler.hpp"
#include <optional>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace myvk::render_graph {

class RenderGraphBase {};
class RGPassBase;

enum class RGInputUsage {
	kPreserve,
	kColorAttachment,
	kDepthAttachmentR,
	kDepthAttachmentRW,
	kInputAttachment,
	kPresent,
	kSampledImage,
	kStorageImageR,
	kStorageImageW,
	kStorageImageRW,
	kUniformBuffer,
	kStorageBufferR,
	kStorageBufferW,
	kStorageBufferRW,
	kIndexBuffer,
	kVertexBuffer,
	kTransferSrc,
	kTransferDst
};

#define CASE(NAME) case RGInputUsage::NAME:
inline constexpr bool RGInputUsageIsDescriptor(RGInputUsage usage) {
	switch (usage) {
		CASE(kInputAttachment)
		CASE(kSampledImage)
		CASE(kStorageImageR)
		CASE(kStorageImageW)
		CASE(kStorageImageRW)
		CASE(kUniformBuffer)
		CASE(kStorageBufferR)
		CASE(kStorageBufferW)
		CASE(kStorageBufferRW)
		return true;
	default:
		return false;
	}
}
inline constexpr VkDescriptorType RGInputUsageGetDescriptorType(RGInputUsage usage) {
	switch (usage) {
		CASE(kInputAttachment)
		return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
		CASE(kSampledImage)
		return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		CASE(kStorageImageR)
		CASE(kStorageImageW)
		CASE(kStorageImageRW)
		return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		CASE(kUniformBuffer)
		return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		CASE(kStorageBufferR)
		CASE(kStorageBufferW)
		CASE(kStorageBufferRW)
		return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	default:
		return VK_DESCRIPTOR_TYPE_MAX_ENUM;
	}
}
inline constexpr bool RGInputUsageIsReadOnly(RGInputUsage usage) {
	switch (usage) {
		CASE(kPresent)
		CASE(kPreserve)
		CASE(kDepthAttachmentR)
		CASE(kInputAttachment)
		CASE(kSampledImage)
		CASE(kStorageImageR)
		CASE(kUniformBuffer)
		CASE(kStorageBufferR)
		CASE(kIndexBuffer)
		CASE(kVertexBuffer)
		CASE(kTransferSrc)
		return true;
	default:
		return false;
	}
}
inline constexpr bool RGInputUsageForBuffer(RGInputUsage usage) {
	switch (usage) {
		CASE(kPreserve)
		CASE(kUniformBuffer)
		CASE(kStorageBufferR)
		CASE(kStorageBufferW)
		CASE(kStorageBufferRW)
		CASE(kIndexBuffer)
		CASE(kVertexBuffer)
		CASE(kTransferSrc)
		CASE(kTransferDst)
		return true;
	default:
		return false;
	}
}
inline constexpr bool RGInputUsageForImage(RGInputUsage usage) {
	switch (usage) {
		CASE(kPreserve)
		CASE(kPresent)
		CASE(kColorAttachment)
		CASE(kDepthAttachmentR)
		CASE(kDepthAttachmentRW)
		CASE(kInputAttachment)
		CASE(kSampledImage)
		CASE(kStorageImageR)
		CASE(kStorageImageW)
		CASE(kStorageImageRW)
		CASE(kTransferSrc)
		CASE(kTransferDst)
		return true;
	default:
		return false;
	}
}
inline constexpr bool RGInputUsageGetImageLayout(RGInputUsage usage) {
	switch (usage) {
		CASE(kPresent)
		return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		CASE(kColorAttachment)
		return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		CASE(kDepthAttachmentR)
		CASE(kDepthAttachmentRW)
		return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		CASE(kInputAttachment)
		CASE(kSampledImage)
		return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		CASE(kStorageImageR)
		CASE(kStorageImageW)
		CASE(kStorageImageRW)
		return VK_IMAGE_LAYOUT_GENERAL;
		CASE(kTransferSrc)
		return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		CASE(kTransferDst)
		return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	default:
		return VK_IMAGE_LAYOUT_UNDEFINED;
	}
}
#undef CASE
inline constexpr bool RGInputUsageAll(RGInputUsage) { return true; }

// Input Usage Operation
using RGInputUsageClass = bool(RGInputUsage);
namespace _details_rg_input_usage_op_ {
template <RGInputUsageClass... Args> struct Union;
template <RGInputUsageClass X, RGInputUsageClass... Args> struct Union<X, Args...> {
	constexpr bool operator()(RGInputUsage x) const { return X(x) || Union<Args...>(x); }
};
template <> struct Union<> {
	constexpr bool operator()(RGInputUsage) const { return false; }
};
template <bool... Args(RGInputUsage)> struct Intersect;
template <RGInputUsageClass X, RGInputUsageClass... Args> struct Intersect<X, Args...> {
	constexpr bool operator()(RGInputUsage x) const { return X(x) && Intersect<Args...>(x); }
};
template <> struct Intersect<> {
	constexpr bool operator()(RGInputUsage) const { return true; }
};
} // namespace _details_rg_input_usage_op_
// Union: A | B
template <RGInputUsageClass... Args> inline constexpr bool RGInputUsageUnion(RGInputUsage x) {
	return _details_rg_input_usage_op_::Union<Args...>(x);
}
// Intersect: A & B
template <RGInputUsageClass... Args> inline constexpr bool RGInputUsageIntersect(RGInputUsage x) {
	return _details_rg_input_usage_op_::Intersect<Args...>(x);
}
// Complement: ~X
template <RGInputUsageClass X> inline constexpr bool RGInputUsageComplement(RGInputUsage x) { return !X(x); }
// Minus: A & (~B)
template <RGInputUsageClass A, RGInputUsageClass B> inline constexpr bool RGInputUsageMinus(RGInputUsage x) {
	return RGInputUsageIntersect<A, RGInputUsageComplement<B>>(x);
}

// Object Base
class RGObjectBase {
private:
	RenderGraphBase *m_render_graph_ptr{};
	std::string_view m_name{};

	inline void set_render_graph_ptr(RenderGraphBase *render_graph_ptr) { m_render_graph_ptr = render_graph_ptr; }
	inline void set_name(std::string_view str) { m_name = str; }

	template <typename, typename, typename...> friend class RGObjectPool;

public:
	inline RGObjectBase() = default;
	inline virtual ~RGObjectBase() = default;

	inline RenderGraphBase *GetRenderGraphPtr() const { return m_render_graph_ptr; }
	inline const std::string_view &GetName() const { return m_name; }

	// Disable Copy and Move
	inline RGObjectBase(const RGObjectBase &r) = delete;
	inline RGObjectBase &operator=(const RGObjectBase &r) = delete;
	inline RGObjectBase(RGObjectBase &&r) = delete;
	inline RGObjectBase &operator=(RGObjectBase &&r) = delete;
};

// Resource & Resource Input
enum class RGResourceType { kImage, kBuffer };
enum class RGResourceState { kManaged, kExternal };
template <RGResourceType Type> inline constexpr bool RGInputUsageForType(RGInputUsage usage) {
	if constexpr (Type == RGResourceType::kImage) {
		return RGInputUsageForImage(usage);
	} else {
		return RGInputUsageForBuffer(usage);
	}
};

class RGResourceBase : virtual public RGObjectBase {
private:
	const RGPassBase *m_producer_pass_ptr{};

	inline void set_producer_pass_ptr(const RGPassBase *producer_pass_ptr) { m_producer_pass_ptr = producer_pass_ptr; }

	template <typename, typename, typename...> friend class RGObjectPool;

public:
	inline ~RGResourceBase() override = default;

	virtual RGResourceType GetType() const = 0;
	virtual RGResourceState GetState() const = 0;
	virtual bool IsAlias() const = 0;
	// virtual bool IsPerFrame() const = 0;
	// virtual void Resize(uint32_t width, uint32_t height) {}

	inline const RGPassBase *GetProducerPassPtr() const { return m_producer_pass_ptr; }
};

// Object Pool
namespace _details_rg_object_pool_ {
template <typename Type> struct TypeTraits {
	constexpr static bool UsePtr = std::is_base_of_v<RGObjectBase, Type> && !std::is_final_v<Type>;
	constexpr static bool UseOptional =
	    !UsePtr && !(std::is_default_constructible_v<Type> && std::is_move_assignable_v<Type>);
	using AlterT =
	    std::conditional_t<UsePtr, std::unique_ptr<Type>, std::conditional_t<UseOptional, std::optional<Type>, Type>>;
	template <typename X> constexpr static bool Match = UsePtr ? std::is_base_of_v<Type, X> : std::is_same_v<Type, X>;
};
template <typename... Types> struct TypeTuple;
template <typename Type, typename... Others> struct TypeTuple<Type, Others...> {
	using T = decltype(std::tuple_cat(std::declval<std::tuple<typename TypeTraits<Type>::AlterT>>(),
	                                  std::declval<typename TypeTuple<Others...>::T>()));
};
template <> struct TypeTuple<> {
	using T = std::tuple<>;
};
} // namespace _details_rg_object_pool_
template <typename RGDerived, typename MainType, typename... OtherTypes> class RGObjectPool {
private:
	using TypeTuple = typename _details_rg_object_pool_::TypeTuple<MainType, OtherTypes...>::T;
	template <std::size_t Index> using GetType = std::tuple_element_t<Index, std::tuple<MainType, OtherTypes...>>;
	template <std::size_t Index> using GetAlterType = std::tuple_element_t<Index, TypeTuple>;
	template <std::size_t Index>
	static constexpr bool kUsePtr = _details_rg_object_pool_::TypeTraits<GetType<Index>>::UsePtr;
	template <std::size_t Index>
	static constexpr bool kUseOptional = _details_rg_object_pool_::TypeTraits<GetType<Index>>::UseOptional;
	template <std::size_t Index, typename T>
	static constexpr bool kTypeMatch = _details_rg_object_pool_::TypeTraits<GetType<Index>>::template Match<T>;

	std::unordered_map<std::string, TypeTuple> m_pool;

	template <std::size_t Index, typename ConsType, typename... Args,
	          typename = std::enable_if_t<kTypeMatch<Index, ConsType>>>
	inline ConsType *initialize(const decltype(m_pool.begin()) &it, Args &&...args) {
		GetAlterType<Index> &ref = std::get<Index>(it->second);
		ConsType *ptr;
		if constexpr (kUsePtr<Index>) {
			auto uptr = std::make_unique<ConsType>(std::forward<Args>(args)...);
			ptr = uptr.get();
			ref = std::move(uptr);
		} else {
			ref = ConsType(std::forward<Args>(args)...);
			if constexpr (kUseOptional<Index>)
				ptr = &(ref.value());
			else
				ptr = &ref;
		}
		using Type = GetType<Index>;

		// Initialize RGObjectBase
		if constexpr (std::is_base_of_v<RGObjectBase, Type>) {
			static_assert(std::is_base_of_v<RenderGraphBase, RGDerived> || std::is_base_of_v<RGObjectBase, RGDerived>);

			auto base_ptr = static_cast<RGObjectBase *>(ptr);
			base_ptr->set_name(it->first);
			if constexpr (std::is_base_of_v<RenderGraphBase, RGDerived>)
				base_ptr->set_render_graph_ptr(dynamic_cast<RenderGraphBase *>(this));
			else
				base_ptr->set_render_graph_ptr(dynamic_cast<RGObjectBase *>(this)->GetRenderGraphPtr());
		}
		// Initialize RGResourceBase
		if constexpr (std::is_base_of_v<RGResourceBase, Type>) {
			auto resource_ptr = static_cast<RGResourceBase *>(ptr);
			if constexpr (std::is_base_of_v<RGPassBase, RGDerived>)
				resource_ptr->set_producer_pass_ptr(dynamic_cast<RGPassBase *>(this));
		}
		return ptr;
	}

public:
	inline RGObjectPool() {
		printf("%s\n", typeid(TypeTuple).name());
	}
	inline virtual ~RGObjectPool() = default;

	// Create Tag and Initialize the Main Object
	template <typename ConsType, typename... Args, typename = std::enable_if_t<kTypeMatch<0, ConsType>>>
	inline ConsType *Create(const std::string &name, Args &&...args) {
		if (m_pool.find(name) != m_pool.end())
			return nullptr;
		auto it = m_pool.insert({name, TypeTuple{}}).first;
		return initialize<0, ConsType, Args...>(it, std::forward<Args>(args)...);
	}
	// Create Tag Only
	inline void CreateTag(const std::string &name) {
		if (m_pool.find(name) != m_pool.end())
			return;
		m_pool.insert({name, TypeTuple{}});
	}
	// Initialize Object of a Tag
	template <std::size_t Index, typename ConsType, typename... Args,
	          typename = std::enable_if_t<kTypeMatch<Index, ConsType>>>
	inline ConsType *Initialize(const std::string &name, Args &&...args) {
		auto it = m_pool.find(name);
		if (it == m_pool.end())
			return nullptr;
		return initialize<Index, ConsType, Args...>(it, std::forward<Args>(args)...);
	}
	// Delete a Tag and its Objects
	inline void Delete(const std::string &name) { m_pool.erase(name); }
	// Get a Object from a Tag
	template <std::size_t Index, typename Type, typename = std::enable_if_t<kTypeMatch<Index, Type>>>
	inline Type *Get(const std::string &name) const {
		auto it = m_pool.find(name);
		if (it == m_pool.end())
			return nullptr;
		const GetAlterType<Index> &ref = std::get<Index>(it->second);
		if constexpr (kUsePtr<Index>)
			return dynamic_cast<Type *>(ref.get());
		else if constexpr (kUseOptional<Index>)
			return ref.has_value() ? dynamic_cast<Type *>(&(ref.value())) : nullptr;
		else
			return &ref;
	}
};

// Buffer and Image
class RGBufferBase : virtual public RGResourceBase {
public:
	~RGBufferBase() override = default;

	inline RGResourceType GetType() const final { return RGResourceType::kBuffer; }
	virtual const Ptr<BufferBase> &GetBuffer(uint32_t frame = 0) const = 0;
};

enum class RGAttachmentLoadOp { kClear, kLoad, kDontCare };
class RGImageBase : virtual public RGResourceBase {
public:
	~RGImageBase() override = default;

	inline RGResourceType GetType() const final { return RGResourceType::kImage; }
	virtual const Ptr<ImageView> &GetImageView(uint32_t frame = 0) const = 0;

	virtual RGAttachmentLoadOp GetLoadOp() const = 0;
	virtual const VkClearValue &GetClearValue() const = 0;
};

// Alias
class RGResourceAliasBase : virtual public RGResourceBase {
private:
	const RGResourceBase *const m_resource;

public:
	inline RGResourceAliasBase(const RGResourceBase *resource)
	    : m_resource{resource->IsAlias() ? dynamic_cast<const RGResourceAliasBase *>(resource)->GetResource()
	                                     : resource} {}
	bool IsAlias() const final { return true; }
	RGResourceState GetState() const final { return m_resource->GetState(); }

	template <typename Type = RGResourceBase, typename = std::enable_if_t<std::is_base_of_v<RGResourceBase, Type>>>
	const Type *GetResource() const {
		return dynamic_cast<const Type *>(m_resource);
	}
	~RGResourceAliasBase() override = default;
};
class RGImageAlias final : public RGResourceAliasBase, public RGImageBase {
public:
	inline explicit RGImageAlias(const RGImageBase *image) : RGResourceAliasBase(image) {}
	inline ~RGImageAlias() final = default;
	inline const Ptr<ImageView> &GetImageView(uint32_t frame = 0) const final {
		return GetResource<RGImageBase>()->GetImageView(frame);
	}
	inline RGAttachmentLoadOp GetLoadOp() const final { return GetResource<RGImageBase>()->GetLoadOp(); }
	virtual const VkClearValue &GetClearValue() const final { return GetResource<RGImageBase>()->GetClearValue(); }
};
class RGBufferAlias final : public RGResourceAliasBase, public RGBufferBase {
public:
	inline explicit RGBufferAlias(const RGBufferBase *image) : RGResourceAliasBase(image) {}
	inline ~RGBufferAlias() final = default;
	inline const Ptr<BufferBase> &GetBuffer(uint32_t frame = 0) const final {
		return GetResource<RGBufferBase>()->GetBuffer(frame);
	}
};

// Resource Input
struct RGInputDescriptorInfo {
	VkShaderStageFlags stage_flags;
	Ptr<Sampler> sampler;
};
class RGInput {
private:
	const RGResourceBase *m_resource_ptr{};
	RGInputUsage m_usage{};
	RGInputDescriptorInfo m_descriptor_info;

	friend class RGDescriptorPassBase;

public:
	inline RGInput() = default;
	inline RGInput(const RGResourceBase *resource_ptr, RGInputUsage usage)
	    : m_resource_ptr{resource_ptr}, m_usage{usage}, m_descriptor_info{} {}
	inline RGInput(const RGResourceBase *resource_ptr, RGInputUsage usage, RGInputDescriptorInfo descriptor_info)
	    : m_resource_ptr{resource_ptr}, m_usage{usage}, m_descriptor_info{std::move(descriptor_info)} {}

	inline const RGResourceBase *GetResource() const { return m_resource_ptr; }
	inline RGInputUsage GetUsage() const { return m_usage; }
	inline const RGInputDescriptorInfo &GetDescriptorInfo() const { return m_descriptor_info; }
};

// Resource Pool
template <typename RGDerived>
class RGResourcePool : protected RGObjectPool<RGDerived, RGBufferBase>, protected RGObjectPool<RGDerived, RGImageBase> {
public:
	inline ~RGResourcePool() override = default;
	template <typename BufferType, typename... Args,
	          typename = std::enable_if_t<std::is_base_of_v<RGBufferBase, BufferType>>>
	inline BufferType *CreateBuffer(const std::string &buffer_name, Args &&...args) {
		return RGObjectPool<RGDerived, RGBufferBase>::template Create<BufferType, Args...>(buffer_name,
		                                                                                   std::forward<Args>(args)...);
	}
	template <typename ImageType, typename... Args,
	          typename = std::enable_if_t<std::is_base_of_v<RGImageBase, ImageType>>>
	inline ImageType *CreateImage(const std::string &image_name, Args &&...args) {
		return RGObjectPool<RGDerived, RGImageBase>::template Create<ImageType, Args...>(image_name,
		                                                                                 std::forward<Args>(args)...);
	}
	template <typename RGType = RGBufferBase, typename = std::enable_if_t<std::is_base_of_v<RGBufferBase, RGType>>>
	inline RGBufferBase *GetBuffer(const std::string &buffer_name) const {
		return RGObjectPool<RGDerived, RGBufferBase>::template Get<0, RGType>(buffer_name);
	}
	template <typename RGType = RGImageBase, typename = std::enable_if_t<std::is_base_of_v<RGImageBase, RGType>>>
	inline RGImageBase *GetImage(const std::string &image_name) const {
		return RGObjectPool<RGDerived, RGImageBase>::template Get<0, RGType>(image_name);
	}
	inline void DeleteBuffer(const std::string &buffer_name) {
		return RGObjectPool<RGDerived, RGBufferBase>::Delete(buffer_name);
	}
	inline void DeleteImage(const std::string &image_name) {
		return RGObjectPool<RGDerived, RGImageBase>::Delete(image_name);
	}
};

// Input Pool
template <typename RGDerived, RGInputUsageClass UsageClass>
class RGInputPool : protected RGObjectPool<RGDerived, RGInput, RGBufferAlias>,
                    protected RGObjectPool<RGDerived, RGInput, RGImageAlias> {
private:
	using ImageInputPool = RGObjectPool<RGDerived, RGInput, RGImageAlias>;
	using BufferInputPool = RGObjectPool<RGDerived, RGInput, RGBufferAlias>;

public:
	inline ~RGInputPool() override = default;
};

// Render Pass
class RGPassBase : virtual public RGObjectBase {};

// Buffers
class RGManagedBuffer final : virtual public RGBufferBase {
public:
	~RGManagedBuffer() override = default;

	const Ptr<BufferBase> &GetBuffer(uint32_t frame = 0) const final { return nullptr; }
	bool IsAlias() const final { return false; }
	RGResourceState GetState() const final { return RGResourceState::kManaged; }
};
} // namespace myvk::render_graph

#endif
