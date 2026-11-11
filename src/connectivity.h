// connectivity.h
// ---------------------------------------------------------------------------
// Two graph-connectivity questions that matter in a flood:
//
//  1. Which places can still reach a shelter at all?        -> BFS
//  2. Which junctions, if they fail, cut people off?        -> Tarjan (DFS)
//
// Both only look at roads that are currently passable.
// ---------------------------------------------------------------------------
#pragma once

#include <functional>
#include <queue>
#include <vector>

#include "graph.h"

// ---------------------------------------------------------------------------
// 1. Breadth-First Search from every open shelter at the same time ("multi-source BFS").
//    reached[i] == true  means node i can drive to an open shelter.
//    reached[i] == false means node i is CUT OFF.
//
//    'removed' lets us ask "what if this junction failed?" by pretending that
//    node does not exist (-1 = nobody removed). Roads are two-way, so searching
//    from the shelters outwards gives the same answer as searching from each zone.
// ---------------------------------------------------------------------------
inline std::vector<bool> reachableFromShelters(const Graph& g, int removed = -1) {
    std::vector<bool> reached(g.nodes.size(), false);
    std::queue<int> q;

    for (int i = 0; i < (int)g.nodes.size(); i++) {
        if (i != removed && g.isOpenShelter(i)) {
            reached[i] = true;
            q.push(i);
        }
    }
    while (!q.empty()) {
        int u = q.front();
        q.pop();
        for (const Neighbour& nb : g.adj[u]) {
            if (nb.to == removed || reached[nb.to]) continue;
            if (!g.passable(nb.edgeId)) continue;
            reached[nb.to] = true;
            q.push(nb.to);
        }
    }
    return reached;
}

// ---------------------------------------------------------------------------
// 2. Tarjan's articulation-point algorithm (one DFS).
//
//    An articulation point is a node whose removal splits the network into more
//    pieces. For every node we store
//        disc[u] = the time DFS first visited u
//        low[u]  = the earliest visit time reachable from u's DFS subtree
//                  using at most one "back edge"
//    A non-root node u is an articulation point if some child v has
//        low[v] >= disc[u]     (v's subtree cannot get around u)
//    The DFS root is one if it has two or more DFS children.
// ---------------------------------------------------------------------------
class ArticulationFinder {
public:
    explicit ArticulationFinder(const Graph& graph)
        : g(graph), disc(graph.nodes.size(), 0), low(graph.nodes.size(), 0),
          isCut(graph.nodes.size(), false) {}

    std::vector<int> run() {
        for (int i = 0; i < (int)g.nodes.size(); i++)
            if (disc[i] == 0) dfs(i, -1);          // the graph may be in several pieces
        std::vector<int> result;
        for (int i = 0; i < (int)g.nodes.size(); i++)
            if (isCut[i]) result.push_back(i);
        return result;
    }

private:
    const Graph&      g;
    std::vector<int>  disc, low;
    std::vector<bool> isCut;
    int               timer = 0;

    void dfs(int u, int parentEdge) {
        disc[u] = low[u] = ++timer;
        int children = 0;

        for (const Neighbour& nb : g.adj[u]) {
            if (!g.passable(nb.edgeId)) continue;      // flooded roads do not exist for us
            if (nb.edgeId == parentEdge) continue;     // do not walk straight back

            int v = nb.to;
            if (disc[v] != 0) {                        // already visited: a back edge
                low[u] = std::min(low[u], disc[v]);
            } else {                                   // unvisited: a tree edge
                children++;
                dfs(v, nb.edgeId);
                low[u] = std::min(low[u], low[v]);
                if (parentEdge != -1 && low[v] >= disc[u]) isCut[u] = true;
            }
        }
        if (parentEdge == -1 && children > 1) isCut[u] = true;   // special rule for the root
    }
};

// A critical junction = an articulation point whose failure would newly cut people off.
struct CriticalJunction {
    int node;
    int newlyStranded;   // people who can reach a shelter now, but could not if this node failed
};

inline std::vector<CriticalJunction> findCriticalJunctions(const Graph& g) {
    std::vector<bool> before = reachableFromShelters(g);
    std::vector<CriticalJunction> result;

    for (int v : ArticulationFinder(g).run()) {
        std::vector<bool> after = reachableFromShelters(g, v);   // simulate v failing
        int stranded = 0;
        for (int u = 0; u < (int)g.nodes.size(); u++)
            if (u != v && before[u] && !after[u]) stranded += g.nodes[u].population;
        if (stranded > 0) result.push_back({v, stranded});
    }
    std::sort(result.begin(), result.end(),
              [](const CriticalJunction& a, const CriticalJunction& b) {
                  return a.newlyStranded > b.newlyStranded;
              });
    return result;
}
