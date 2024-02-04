//
// Created by adamyuan on 2/4/24.
//

#pragma once
#ifndef MYVK_GRAPH_HPP
#define MYVK_GRAPH_HPP

#include <cinttypes>
#include <optional>
#include <ranges>
#include <unordered_map>
#include <vector>

template <typename VertexID_T, typename Edge_T> class Graph {
private:
	template <typename K, typename V> using Map = std::unordered_map<K, V>;
	struct VertexInfo {
		std::vector<std::size_t> in, out;
	};
	struct EdgeInfo {
		Edge_T e;
		VertexID_T from, to;
	};
	Map<VertexID_T, VertexInfo> m_vertices;
	std::vector<std::optional<EdgeInfo>> m_edges;

public:
	void AddVertex(VertexID_T vertex) { m_vertices.insert({vertex, VertexInfo{}}); }
	std::size_t AddEdge(VertexID_T from, VertexID_T to, Edge_T edge) {
		std::size_t edge_id = m_edges.size();
		m_edges.emplace_back(EdgeInfo{
		    .e = std::move(edge),
		    .from = from,
		    .to = to,
		});
		m_vertices[from].out.push_back(edge_id);
		m_vertices[to].in.push_back(edge_id);
		return edge_id;
	}
	void RemoveEdge(std::size_t edge_id) { m_edges[edge_id] = std::nullopt; }
	bool HasVertex(VertexID_T vertex) const { return m_vertices.count(vertex); }

	auto GetOutEdges(VertexID_T vertex, auto &&filter) const {
		return m_vertices.at(vertex).out | std::views::filter([this, &filter](std::size_t edge_id) {
			       return m_edges[edge_id].has_value() && filter(m_edges[edge_id]->e);
		       }) |
		       std::views::transform(
		           [this](std::size_t edge_id) -> std::tuple<VertexID_T, const Edge_T &, std::size_t> {
			           return std::make_tuple(m_edges[edge_id]->to, std::cref(m_edges[edge_id]->e), edge_id);
		           });
	}
	auto GetOutEdges(VertexID_T vertex) const {
		return GetOutEdges(vertex, [](auto &&) -> bool { return true; });
	}

	auto GetInEdges(VertexID_T vertex, auto &&filter) const {
		return m_vertices.at(vertex).in | std::views::filter([this, &filter](std::size_t edge_id) {
			       return m_edges[edge_id].has_value() && filter(m_edges[edge_id]->e);
		       }) |
		       std::views::transform(
		           [this](std::size_t edge_id) -> std::tuple<VertexID_T, const Edge_T &, std::size_t> {
			           return std::make_tuple(m_edges[edge_id]->from, std::cref(m_edges[edge_id]->e), edge_id);
		           });
	}
	auto GetInEdges(VertexID_T vertex) const {
		return GetInEdges(vertex, [](auto &&) -> bool { return true; });
	}

	auto GetEdges(auto &&filter) const {
		return m_edges | std::views::filter([&filter](const std::optional<EdgeInfo> &opt_edge_info) {
			       return opt_edge_info && filter(opt_edge_info->e);
		       }) |
		       std::views::transform([this](const std::optional<EdgeInfo> &opt_edge_info)
		                                 -> std::tuple<VertexID_T, VertexID_T, const Edge_T &, std::size_t> {
			       std::size_t edge_id = &opt_edge_info - m_edges.data();
			       return std::make_tuple(opt_edge_info->from, opt_edge_info->to, std::cref(opt_edge_info->e), edge_id);
		       });
	}
	auto GetEdges() const {
		return GetEdges([](auto &&) -> bool { return true; });
	}

	VertexID_T GetFromVertex(std::size_t edge_id) const { return m_edges[edge_id]->from; }
	VertexID_T GetToVertex(std::size_t edge_id) const { return m_edges[edge_id]->to; }
	const Edge_T GetEdge(std::size_t edge_id) const { return m_edges[edge_id]->e; }

	auto GetVertices(auto &&filter) const {
		return m_vertices | std::views::transform([](const std::pair<VertexID_T, VertexInfo> &pair) -> VertexID_T {
			       return pair.first;
		       }) |
		       std::views::filter([&filter](VertexID_T vertex_id) { return filter(vertex_id); });
	}
	auto GetVertices() const {
		return m_vertices | std::views::transform(
		                        [](const std::pair<VertexID_T, VertexInfo> &pair) -> VertexID_T { return pair.first; });
	}

	void WriteGraphViz(std::ostream &out, auto &&vertex_name, auto &&edge_label) const;

	// Algorithms
	struct KahnTopologicalSortResult {
		std::vector<VertexID_T> sorted;
		bool is_dag;
	};
	KahnTopologicalSortResult KahnTopologicalSort(auto &&edge_filter) const;
};

#include "Graph.tpp"

#endif // MYVK_GRAPH_HPP
