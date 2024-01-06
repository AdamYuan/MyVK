#pragma once
#ifndef MYVK_RG_COLLECTOR_HPP
#define MYVK_RG_COLLECTOR_HPP

#include <map>

#include "../ErrorMacro.hpp"
#include <myvk_rg/interface/RenderGraph.hpp>

using namespace myvk_rg::interface;

class Collector {
private:
	std::map<GlobalKey, const PassBase *> m_passes;
	std::map<GlobalKey, const InputBase *> m_inputs;
	std::map<GlobalKey, const ResourceBase *> m_resources;

	template <typename Container> CompileResult<void> collect_resources(const Container &pool);
	template <typename Container> CompileResult<void> collect_passes(const Container &pool);
	template <typename Container> CompileResult<void> collect_inputs(const Container &pool);

public:
	CompileResult<void> Collect(const RenderGraphBase &rg);
	inline const auto &GetPasses() const { return m_passes; }
	inline const auto &GetInputs() const { return m_inputs; }
	inline const auto &GetResources() const { return m_resources; }
};

#endif
