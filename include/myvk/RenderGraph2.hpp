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

	template <typename, typename...> friend class RGObjectPool;

public:
	inline RGObjectBase() = default;
	inline virtual ~RGObjectBase() = default;

	inline RenderGraphBase *GetRenderGraphPtr() const { return m_render_graph_ptr; }
	inline const std::string_view &GetName() const { return m_name; }

	// Disable Copy
	inline RGObjectBase(RGObjectBase &&r) noexcept = default;
	// inline RGObjectBase &operator=(RGObjectBase &&r) noexcept = default;
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
	RGPassBase *m_producer_pass_ptr{};

	inline void set_producer_pass_ptr(RGPassBase *producer_pass_ptr) { m_producer_pass_ptr = producer_pass_ptr; }

	template <typename, typename...> friend class RGObjectPool;

public:
	inline ~RGResourceBase() override = default;
	inline RGResourceBase() = default;
	inline RGResourceBase(RGResourceBase &&) noexcept = default;

	virtual RGResourceType GetType() const = 0;
	virtual RGResourceState GetState() const = 0;
	virtual bool IsAlias() const = 0;
	// virtual bool IsPerFrame() const = 0;
	// virtual void Resize(uint32_t width, uint32_t height) {}

	inline RGPassBase *GetProducerPassPtr() const { return m_producer_pass_ptr; }
};

// Object Pool
namespace _details_rg_object_pool_ {
template <typename Type> struct TypeTraits {
	constexpr static bool kIsRGObject = std::is_base_of_v<RGObjectBase, Type>;
	constexpr static bool kUsePtr = (kIsRGObject && !std::is_final_v<Type>) || !std::is_move_constructible_v<Type>;
	constexpr static bool kUseOptional = !kUsePtr && (kIsRGObject || !std::is_default_constructible_v<Type>);
	constexpr static bool kUsePlain = !kUsePtr && !kUseOptional;
	using AlterT =
	    std::conditional_t<kUsePtr, std::unique_ptr<Type>, std::conditional_t<kUseOptional, std::optional<Type>, Type>>;
	template <typename X> constexpr static bool Match = kUsePtr ? std::is_base_of_v<Type, X> : std::is_same_v<Type, X>;
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
template <typename RGDerived, typename... Types> class RGObjectPool {
private:
	using TypeTuple = typename _details_rg_object_pool_::TypeTuple<Types...>::T;
	template <std::size_t Index> using GetType = std::tuple_element_t<Index, std::tuple<Types...>>;
	template <std::size_t Index> using GetAlterType = std::tuple_element_t<Index, TypeTuple>;
	template <std::size_t Index>
	static constexpr bool kUsePtr = _details_rg_object_pool_::TypeTraits<GetType<Index>>::kUsePtr;
	template <std::size_t Index>
	static constexpr bool kUseOptional = _details_rg_object_pool_::TypeTraits<GetType<Index>>::kUseOptional;
	template <std::size_t Index>
	static constexpr bool kIsRGObject = _details_rg_object_pool_::TypeTraits<GetType<Index>>::kIsRGObject;
	template <std::size_t Index, typename T>
	static constexpr bool kTypeMatch = _details_rg_object_pool_::TypeTraits<GetType<Index>>::template Match<T>;

	std::unordered_map<std::string, TypeTuple> m_pool;

	template <std::size_t Index, typename ConsType, typename... Args, typename MapIterator,
	          typename = std::enable_if_t<kTypeMatch<Index, ConsType>>>
	inline ConsType *initialize(const MapIterator &it, Args &&...args) {
		GetAlterType<Index> &ref = std::get<Index>(it->second);
		ConsType *ptr;
		if constexpr (kUsePtr<Index>) {
			auto uptr = std::make_unique<ConsType>(std::forward<Args>(args)...);
			ptr = uptr.get();
			ref = std::move(uptr);
		} else if constexpr (kUseOptional<Index>) {
			ref.emplace(std::forward<Args>(args)...);
			ptr = &(ref.value());
		} else {
			ref = ConsType(std::forward<Args>(args)...);
			ptr = &ref;
		}
		using Type = GetType<Index>;

		// Initialize RGObjectBase
		if constexpr (std::is_base_of_v<RGObjectBase, Type>) {
			static_assert(std::is_base_of_v<RenderGraphBase, RGDerived> || std::is_base_of_v<RGObjectBase, RGDerived>);

			auto base_ptr = static_cast<RGObjectBase *>(ptr);
			base_ptr->set_name(it->first);
			if constexpr (std::is_base_of_v<RenderGraphBase, RGDerived>)
				base_ptr->set_render_graph_ptr((RenderGraphBase *)static_cast<const RGDerived *>(this));
			else
				base_ptr->set_render_graph_ptr(
				    ((RGObjectBase *)static_cast<const RGDerived *>(this))->GetRenderGraphPtr());
		}
		// Initialize RGResourceBase
		if constexpr (std::is_base_of_v<RGResourceBase, Type>) {
			auto resource_ptr = static_cast<RGResourceBase *>(ptr);
			if constexpr (std::is_base_of_v<RGPassBase, RGDerived>)
				resource_ptr->set_producer_pass_ptr((RGPassBase *)static_cast<const RGDerived *>(this));
		}
		return ptr;
	}
	template <std::size_t Index, typename Type, typename MapIterator,
	          typename = std::enable_if_t<kTypeMatch<Index, Type>>>
	inline Type *get(const MapIterator &it) const {
		const GetAlterType<Index> &ref = std::get<Index>(it->second);
		if constexpr (kUsePtr<Index>)
			return (Type *)dynamic_cast<const Type *>(ref.get());
		else if constexpr (kUseOptional<Index>)
			return ref.has_value() ? (Type *)(&(ref.value())) : nullptr;
		else
			return (Type *)(&ref);
	}

public:
	inline RGObjectPool() {
		static_assert(std::is_default_constructible_v<TypeTuple> && std::is_move_constructible_v<TypeTuple>);
		// TODO: Debug
		printf("%s\n", typeid(TypeTuple).name());
	}
	inline RGObjectPool(RGObjectPool &&) noexcept = default;
	inline virtual ~RGObjectPool() = default;

protected:
	// Create Tag and Initialize the Main Object
	template <std::size_t Index, typename ConsType, typename... Args,
	          typename = std::enable_if_t<kTypeMatch<Index, ConsType>>>
	inline ConsType *CreateAndInitialize(const std::string &name, Args &&...args) {
		if (m_pool.find(name) != m_pool.end())
			return nullptr;
		auto it = m_pool.insert({name, TypeTuple{}}).first;
		return initialize<Index, ConsType, Args...>(it, std::forward<Args>(args)...);
	}
	// Create Tag Only
	inline void Create(const std::string &name) {
		if (m_pool.find(name) != m_pool.end())
			return;
		m_pool.insert(std::make_pair(name, TypeTuple{}));
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
	// Get an Object from a Tag, if not Initialized, Initialize it.
	template <std::size_t Index, typename ConsType, typename... Args,
	          typename = std::enable_if_t<kTypeMatch<Index, ConsType> && kIsRGObject<Index>>>
	inline ConsType *InitializeOrGet(const std::string &name, Args &&...args) {
		auto it = m_pool.find(name);
		if (it == m_pool.end())
			return nullptr;
		auto ret = (ConsType *)get<Index, ConsType>(it);
		if (!ret)
			ret = initialize<Index, ConsType, Args...>(it, std::forward<Args>(args)...);
		return ret;
	}
	// Delete a Tag and its Objects
	inline void Delete(const std::string &name) { m_pool.erase(name); }
	// Get an Object from a Tag
	template <std::size_t Index, typename Type, typename = std::enable_if_t<kTypeMatch<Index, Type>>>
	inline Type *Get(const std::string &name) const {
		auto it = m_pool.find(name);
		if (it == m_pool.end())
			return nullptr;
		return (Type *)get<Index, Type>(it);
	}
};

// Buffer and Image
class RGBufferBase : virtual public RGResourceBase {
public:
	inline ~RGBufferBase() override = default;
	inline RGBufferBase() = default;
	inline RGBufferBase(RGBufferBase &&) noexcept = default;

	inline RGResourceType GetType() const final { return RGResourceType::kBuffer; }
	virtual const Ptr<BufferBase> &GetBuffer(uint32_t frame = 0) const = 0;
};

enum class RGAttachmentLoadOp { kClear, kLoad, kDontCare };
class RGImageBase : virtual public RGResourceBase {
public:
	inline ~RGImageBase() override = default;
	inline RGImageBase() = default;
	inline RGImageBase(RGImageBase &&) noexcept = default;

	inline RGResourceType GetType() const final { return RGResourceType::kImage; }
	virtual const Ptr<ImageView> &GetImageView(uint32_t frame = 0) const = 0;

	virtual RGAttachmentLoadOp GetLoadOp() const = 0;
	virtual const VkClearValue &GetClearValue() const = 0;
};

// Alias
class RGResourceAliasBase : virtual public RGResourceBase {
private:
	RGResourceBase *m_resource;

public:
	inline explicit RGResourceAliasBase(RGResourceBase *resource)
	    : m_resource{resource->IsAlias() ? dynamic_cast<RGResourceAliasBase *>(resource)->GetResource() : resource} {}
	inline RGResourceAliasBase(RGResourceAliasBase &&) noexcept = default;

	bool IsAlias() const final { return true; }
	RGResourceState GetState() const final { return m_resource->GetState(); }

	template <typename Type = RGResourceBase, typename = std::enable_if_t<std::is_base_of_v<RGResourceBase, Type>>>
	Type *GetResource() const {
		return dynamic_cast<Type *>(m_resource);
	}
	~RGResourceAliasBase() override = default;
};
class RGImageAlias final : public RGResourceAliasBase, public RGImageBase {
public:
	inline explicit RGImageAlias(RGImageBase *image) : RGResourceAliasBase(image) {}
	inline RGImageAlias(RGImageAlias &&) noexcept = default;
	inline ~RGImageAlias() final = default;

	inline const Ptr<ImageView> &GetImageView(uint32_t frame = 0) const final {
		return GetResource<RGImageBase>()->GetImageView(frame);
	}
	inline RGAttachmentLoadOp GetLoadOp() const final { return GetResource<RGImageBase>()->GetLoadOp(); }
	virtual const VkClearValue &GetClearValue() const final { return GetResource<RGImageBase>()->GetClearValue(); }
};
class RGBufferAlias final : public RGResourceAliasBase, public RGBufferBase {
public:
	inline explicit RGBufferAlias(RGBufferBase *image) : RGResourceAliasBase(image) {}
	inline RGBufferAlias(RGBufferAlias &&) = default;
	inline ~RGBufferAlias() final = default;

	inline const Ptr<BufferBase> &GetBuffer(uint32_t frame = 0) const final {
		return GetResource<RGBufferBase>()->GetBuffer(frame);
	}
};

// Resource Input
// TODO: where to use RGInputDescriptorInfo?
struct RGInputDescriptorInfo {
	VkShaderStageFlags stage_flags;
	Ptr<Sampler> sampler;
};
class RGInput {
private:
	RGResourceBase *m_resource_ptr{};
	RGInputUsage m_usage{};

public:
	inline RGInput() = default;
	RGInput(RGResourceBase *resource_ptr, RGInputUsage usage) : m_resource_ptr{resource_ptr}, m_usage{usage} {}

	inline RGResourceBase *GetResource() const { return m_resource_ptr; }
	inline RGInputUsage GetUsage() const { return m_usage; }
};

// Resource Pool
template <typename RGDerived> class RGResourcePool : public RGObjectPool<RGDerived, RGBufferBase, RGImageBase> {
private:
	using ResourcePool = RGObjectPool<RGDerived, RGBufferBase, RGImageBase>;

public:
	inline RGResourcePool() = default;
	inline RGResourcePool(RGResourcePool &&) noexcept = default;
	inline ~RGResourcePool() override = default;

protected:
	template <
	    typename Type, typename... Args,
	    typename = std::enable_if_t<std::is_base_of_v<RGBufferBase, Type> || std::is_base_of_v<RGImageBase, Type>>>
	inline Type *CreateResource(const std::string &name, Args &&...args) {
		if constexpr (std::is_base_of_v<RGBufferBase, Type>)
			return ResourcePool::template CreateAndInitialize<0, Type, Args...>(name, std::forward<Args>(args)...);
		else
			return ResourcePool::template CreateAndInitialize<1, Type, Args...>(name, std::forward<Args>(args)...);
	}
	inline void DeleteResource(const std::string &name) { return ResourcePool::Delete(name); }

	inline RGBufferBase *GetBufferResource(const std::string &buffer_name) const {
		return ResourcePool::template Get<0, RGBufferBase>(buffer_name);
	}
	inline RGImageBase *GetImageResource(const std::string &image_name) const {
		return ResourcePool::template Get<1, RGImageBase>(image_name);
	}
};

// Input Pool
template <typename RGDerived> class RGInputPool : public RGObjectPool<RGDerived, RGInput, RGBufferAlias, RGImageAlias> {
private:
	using InputPool = RGObjectPool<RGDerived, RGInput, RGBufferAlias, RGImageAlias>;

	template <typename RGType> inline RGType *create_output(const std::string &input_name) {
		// RGType can only be RGBufferBase or RGImageBase
		constexpr RGResourceType kResType =
		    std::is_base_of_v<RGImageBase, RGType> ? RGResourceType::kImage : RGResourceType::kBuffer;

		static_assert(std::is_base_of_v<RGPassBase, RGDerived>);
		const RGInput *input = InputPool::template Get<0, RGInput>(input_name);
		if (!input)
			return nullptr;

		if (input->GetResource()->GetProducerPassPtr() == (RGPassBase *)static_cast<RGDerived *>(this))
			return kResType == input->GetResource()->GetType() ? dynamic_cast<RGType *>(input->GetResource()) : nullptr;
		else {
			if constexpr (kResType == RGResourceType::kBuffer) {
				return (input->GetResource()->GetType() == kResType)
				           ? InputPool::template InitializeOrGet<1, RGBufferAlias>(
				                 input_name, dynamic_cast<RGBufferBase *>(input->GetResource()))
				           : nullptr;
			} else {
				return (input->GetResource()->GetType() == kResType)
				           ? InputPool::template InitializeOrGet<2, RGImageAlias>(
				                 input_name, dynamic_cast<RGImageBase *>(input->GetResource()))
				           : nullptr;
			}
		}
	}

public:
	inline RGInputPool() = default;
	inline RGInputPool(RGInputPool &&) noexcept = default;
	inline ~RGInputPool() override = default;

protected:
	template <RGInputUsage Usage, typename = std::enable_if_t<RGInputUsageForBuffer(Usage)>>
	inline RGInput *AddInput(const std::string &input_name, RGBufferBase *buffer) {
		return InputPool::template CreateAndInitialize<0, RGInput>(input_name, buffer, Usage);
	}
	template <RGInputUsage Usage, typename = std::enable_if_t<RGInputUsageForImage(Usage)>>
	inline RGInput *AddInput(const std::string &input_name, RGImageBase *image) {
		return InputPool::template CreateAndInitialize<0, RGInput>(input_name, image, Usage);
	}
	inline RGInput *GetInput(const std::string &input_name) const {
		return InputPool::template Get<0, RGInput>(input_name);
	}
	inline void RemoveInput(const std::string &input_name) { InputPool::Delete(input_name); }

	inline RGBufferBase *CreateBufferOutput(const std::string &input_name) {
		return create_output<RGBufferBase>(input_name);
	}
	inline RGImageBase *CreateImageOutput(const std::string &input_name) {
		return create_output<RGImageBase>(input_name);
	}
};

/* template <typename RGDerived>
class RGInputSlot {
    // Add...
}; */

// Descriptor Pool

// Render Pass
class RGPassBase : virtual public RGObjectBase {};

// Buffers
class RGManagedBuffer final : virtual public RGBufferBase {
public:
	inline RGManagedBuffer() = default;
	inline RGManagedBuffer(RGManagedBuffer &&) noexcept = default;
	~RGManagedBuffer() override = default;

	const Ptr<BufferBase> &GetBuffer(uint32_t frame = 0) const final {
		static Ptr<BufferBase> x;
		return x;
	}
	bool IsAlias() const final { return false; }
	RGResourceState GetState() const final { return RGResourceState::kManaged; }
};
} // namespace myvk::render_graph

#endif
