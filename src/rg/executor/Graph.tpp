#include <iostream>
#include <queue>

namespace myvk_rg::executor {

template <typename VertexID_T, typename Edge_T>
void Graph<VertexID_T, Edge_T>::WriteGraphViz(std::ostream &out, auto &&vertex_name, auto &&edge_label) const {
	out << "digraph{" << std::endl;
	for (auto vertex : GetVertices())
		out << '\"' << vertex_name(vertex) << "\";" << std::endl;
	for (auto [from, to, e, id] : GetEdges()) {
		out << '\"' << vertex_name(from) << "\"->\"" << vertex_name(to) << "\"[label=\"" << edge_label(e) << "\"];"
		    << std::endl;
	}
	out << "}" << std::endl;
}

template <typename VertexID_T, typename Edge_T>
Graph<VertexID_T, Edge_T>::KahnTopologicalSortResult
Graph<VertexID_T, Edge_T>::KahnTopologicalSort(auto &&edge_filter, std::span<const VertexID_T> start_vertices) const {
	std::vector<VertexID_T> sorted;

	Map<VertexID_T, std::size_t> in_degrees;
	for (auto vertex : GetVertices())
		in_degrees[vertex] = 0;
	for (auto [_, to, _1, _2] : GetEdges(edge_filter)) {
		++in_degrees[to];
	}

	std::queue<VertexID_T> queue;
	if (start_vertices.empty()) {
		for (auto [vert, in_deg] : in_degrees)
			if (in_deg == 0)
				queue.push(vert);
	} else {
		for (auto vert : start_vertices)
			if (in_degrees.at(vert) == 0)
				queue.push(vert);
	}

	while (!queue.empty()) {
		VertexID_T vertex = queue.front();
		sorted.push_back(vertex);
		queue.pop();

		for (auto [to, _, _1] : GetOutEdges(vertex, edge_filter)) {
			if (--in_degrees[to] == 0)
				queue.push(to);
		}
	}

	bool is_dag = sorted.size() == m_vertices.size();
	return {
	    .sorted = std::move(sorted),
	    .is_dag = is_dag,
	};
}

template <typename VertexID_T, typename Edge_T>
Relation Graph<VertexID_T, Edge_T>::TransitiveClosure(auto &&edge_filter, auto &&get_vertex_topo_order,
                                                      auto &&get_topo_order_vertex) const {
	Relation relation{m_vertices.size(), m_vertices.size()};

	for (std::size_t topo_order = m_vertices.size() - 1; ~topo_order; --topo_order) {
		for (auto [from, _, _1] : GetInEdges(get_topo_order_vertex(topo_order), edge_filter)) {
			std::size_t from_topo_order = get_vertex_topo_order(from);
			// assert(from_topo_order < topo_order)
			relation.Add(from_topo_order, topo_order);   // from -> cur
			relation.Apply(topo_order, from_topo_order); // forall x, cur -> x ==> from -> x
		}
	}

	return relation;
}

} // namespace myvk_rg::executor