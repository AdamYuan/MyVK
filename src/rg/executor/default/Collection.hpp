#pragma once
#ifndef MYVK_RG_COLLECTOR_HPP
#define MYVK_RG_COLLECTOR_HPP

#include <map>

#include "Error.hpp"
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
	std::vector<InputInfo> m_input_infos;
	std::vector<ResourceInfo> m_resource_infos;

	template <typename Container> void collect_resources(const Container &pool);
	template <typename Container> void collect_passes(const Container &pool);
	template <typename Container> void collect_inputs(const Container &pool);
	void make_infos();

public:
	static Collection Create(const RenderGraphBase &rg);

	inline const PassBase *FindPass(const GlobalKey &key) const {
		auto it = m_passes.find(key);
		if (it == m_passes.end())
			Throw(error::PassNotFound{.key = key});
		return it->second;
	}
	inline const InputBase *FindInput(const GlobalKey &key) const {
		auto it = m_inputs.find(key);
		if (it == m_inputs.end())
			Throw(error::InputNotFound{.key = key});
		return it->second;
	}
	inline const ResourceBase *FindResource(const GlobalKey &key) const {
		auto it = m_resources.find(key);
		if (it == m_resources.end())
			Throw(error::ResourceNotFound{.key = key});
		return it->second;
	}

	inline const ImageBase *FindAliasSource(const RawImageAlias &alias) const {
		const ResourceBase *p_resource = FindResource(alias.GetSourceKey());
		return p_resource->Visit(overloaded([](const ImageBase *i) -> const ImageBase * { return i; },
		                                    [&](const auto *r) -> const ImageBase * {
			                                    Throw(error::AliasNoMatch{.alias = alias, .actual_type = r->GetType()});
			                                    return nullptr;
		                                    }));
	}
	inline const ImageInput *FindAliasSource(const OutputImageAlias &alias) const {
		const InputBase *p_input = FindInput(alias.GetSourceKey());
		return p_input->Visit(overloaded([](const ImageInput *i) -> const ImageInput * { return i; },
		                                 [&](const auto *r) -> const ImageInput * {
			                                 Throw(error::AliasNoMatch{.alias = alias, .actual_type = r->GetType()});
			                                 return nullptr;
		                                 }));
	}
	inline const BufferBase *FindAliasSource(const RawBufferAlias &alias) const {
		const ResourceBase *p_resource = FindResource(alias.GetSourceKey());
		return p_resource->Visit(overloaded([](const BufferBase *i) -> const BufferBase * { return i; },
		                                    [&](const auto *r) -> const BufferBase * {
			                                    Throw(error::AliasNoMatch{.alias = alias, .actual_type = r->GetType()});
			                                    return nullptr;
		                                    }));
	}
	inline const BufferInput *FindAliasSource(const OutputBufferAlias &alias) const {
		const InputBase *p_input = FindInput(alias.GetSourceKey());
		return p_input->Visit(overloaded([](const BufferInput *i) -> const BufferInput * { return i; },
		                                 [&](const auto *r) -> const BufferInput * {
			                                 Throw(error::AliasNoMatch{.alias = alias, .actual_type = r->GetType()});
			                                 return nullptr;
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

} // namespace default_executor

#endif
