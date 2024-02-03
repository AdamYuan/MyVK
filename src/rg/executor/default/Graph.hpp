//
// Created by adamyuan on 2/4/24.
//

#pragma once
#ifndef MYVK_GRAPH_HPP
#define MYVK_GRAPH_HPP

#include <cinttypes>
#include <optional>
#include <unordered_map>
#include <vector>

template <typename VertexID_T, typename Edge_T, template <typename, typename> typename Map = std::unordered_map>
class Graph {
private:
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
	std::size_t AddEdge(VertexID_T from, VertexID_T to, Edge_T edge) {
		std::size_t edge_id = m_edges.size();
		m_edges.push_back(EdgeInfo{
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
};

#endif // MYVK_GRAPH_HPP
