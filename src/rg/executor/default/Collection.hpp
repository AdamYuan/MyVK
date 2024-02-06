#pragma once
#ifndef MYVK_RG_COLLECTOR_HPP
#define MYVK_RG_COLLECTOR_HPP

#include <map>

#include "../ErrorMacro.hpp"
#include <myvk_rg/interface/RenderGraph.hpp>

#include "Info.hpp"

namespace default_executor {

using namespace myvk_rg::interface;

class Collection {
private:
	std::map<GlobalKey, const PassBase *> m_passes;
	std::map<GlobalKey, const InputBase *> m_inputs;
	std::map<GlobalKey, const ResourceBase *> m_resources;
	std::vector<PassInfo> m_pass_infos;
	std::vector<ResourceInfo> m_resource_infos;

	template <typename Container> CompileResult<void> collect_resources(const Container &pool);
	template <typename Container> CompileResult<void> collect_passes(const Container &pool);
	template <typename Container> CompileResult<void> collect_inputs(const Container &pool);
	void make_infos();

public:
	static CompileResult<Collection> Create(const RenderGraphBase &rg);

	inline CompileResult<const PassBase *> FindPass(const GlobalKey &key) const {
		auto it = m_passes.find(key);
		if (it == m_passes.end())
			return error::PassNotFound{.key = key};
		return it->second;
	}
	inline CompileResult<const InputBase *> FindInput(const GlobalKey &key) const {
		auto it = m_inputs.find(key);
		if (it == m_inputs.end())
			return error::InputNotFound{.key = key};
		return it->second;
	}
	inline CompileResult<const ResourceBase *> FindResource(const GlobalKey &key) const {
		auto it = m_resources.find(key);
		if (it == m_resources.end())
			return error::ResourceNotFound{.key = key};
		return it->second;
	}

	inline CompileResult<const ImageBase *> FindAliasSource(const RawImageAlias &alias) const {
		const ResourceBase *p_resource;
		UNWRAP_ASSIGN(p_resource, FindResource(alias.GetSourceKey()));
		return p_resource->Visit(overloaded([](const ImageBase *i) -> CompileResult<const ImageBase *> { return i; },
		                                    [&](const auto *r) -> CompileResult<const ImageBase *> {
			                                    return error::AliasNoMatch{.alias = alias, .actual_type = r->GetType()};
		                                    }));
	}
	inline CompileResult<const ImageInput *> FindAliasSource(const OutputImageAlias &alias) const {
		const InputBase *p_input;
		UNWRAP_ASSIGN(p_input, FindInput(alias.GetSourceKey()));
		return p_input->Visit(overloaded([](const ImageInput *i) -> CompileResult<const ImageInput *> { return i; },
		                                 [&](const auto *r) -> CompileResult<const ImageInput *> {
			                                 return error::AliasNoMatch{.alias = alias, .actual_type = r->GetType()};
		                                 }));
	}
	inline CompileResult<const BufferBase *> FindAliasSource(const RawBufferAlias &alias) const {
		const ResourceBase *p_resource;
		UNWRAP_ASSIGN(p_resource, FindResource(alias.GetSourceKey()));
		return p_resource->Visit(overloaded([](const BufferBase *i) -> CompileResult<const BufferBase *> { return i; },
		                                    [&](const auto *r) -> CompileResult<const BufferBase *> {
			                                    return error::AliasNoMatch{.alias = alias, .actual_type = r->GetType()};
		                                    }));
	}
	inline CompileResult<const BufferInput *> FindAliasSource(const OutputBufferAlias &alias) const {
		const InputBase *p_input;
		UNWRAP_ASSIGN(p_input, FindInput(alias.GetSourceKey()));
		return p_input->Visit(overloaded([](const BufferInput *i) -> CompileResult<const BufferInput *> { return i; },
		                                 [&](const auto *r) -> CompileResult<const BufferInput *> {
			                                 return error::AliasNoMatch{.alias = alias, .actual_type = r->GetType()};
		                                 }));
	}
	/* inline CompileResult<std::variant<const ImageBase *, const ImageInput *>>
	FindAliasSource(const ImageAliasBase &alias) const {
	    return alias.Visit(
	        [this](const auto *alias) -> CompileResult<std::variant<const ImageBase *, const ImageInput *>> {
	            std::variant<const ImageBase *, const ImageInput *> v;
	            UNWRAP_ASSIGN(v, FindAliasSource(*alias));
	            return v;
	        });
	} */
};

} // namespace myvk_rg::executor

#endif
