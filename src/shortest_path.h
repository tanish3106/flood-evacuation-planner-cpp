// shortest_path.h
// ---------------------------------------------------------------------------
// Dijkstra's algorithm and A* search in one function.
//
//   Dijkstra : always expand the cheapest node found so far.
//   A*       : same, but expand the node with the smallest  g + h  where
//              g = cost so far, h = optimistic guess of the cost still to go.
//
// Dijkstra is simply A* with h = 0, so one function does both.
//
// Destination: the nearest *open* shelter (not full). We do not need to run the
// algorithm once per shelter: the search stops at the first shelter it settles,
// and because nodes are settled in order of cost, that shelter is the cheapest.
//
// Two cost modes let us compare against an ordinary map application:
//   RISK_AWARE    : cost = driving time x flood-risk multiplier, flooded or
//                   blocked roads are avoided, full shelters are skipped.
//   DISTANCE_ONLY : cost = road length only, floods and full shelters are
//                   ignored (how a normal route planner behaves).
// ---------------------------------------------------------------------------
#pragma once

#include <functional>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

#include "graph.h"

enum class CostMode { RISK_AWARE, DISTANCE_ONLY };

struct PathResult {
    bool             found = false;
    std::vector<int> nodes;           // node ids from the start to the shelter
    std::vector<int> edges;           // road ids in the same order
    int              destination = -1;
    double           distance = 0;    // metres
    double           minutes = 0;     // flood-adjusted travel time
    double           maxDepth = 0;    // deepest water on the route (metres)
    int              floodedRoads = 0;  // roads on the route with some water on them
    int              blockedRoads = 0;  // roads on the route that are impassable
    int              explored = 0;    // how many nodes the search had to settle
};

inline PathResult findPath(const Graph& g, int source, bool useAStar, CostMode mode) {
    PathResult result;
    const int n = (int)g.nodes.size();
    const bool risk = (mode == CostMode::RISK_AWARE);
    if (source < 0 || source >= n) return result;

    // 1. Which nodes count as destinations?
    std::vector<int> targets;
    for (int i = 0; i < n; i++) {
        bool isShelter = g.nodes[i].type == NodeType::SHELTER;
        if (isShelter && (!risk || !g.nodes[i].full)) targets.push_back(i);
    }
    if (targets.empty()) return result;

    std::vector<bool> isTarget(n, false);
    for (int t : targets) isTarget[t] = true;

    // 2. The heuristic h(node): straight-line distance to the closest destination.
    //    It never over-estimates, because a road cannot be shorter than a straight line
    //    and the risk multiplier is always >= 1. That is what keeps A* correct.
    auto h = [&](int node) {
        if (!useAStar) return 0.0;
        double best = std::numeric_limits<double>::infinity();
        for (int t : targets) best = std::min(best, g.straightLine(node, t));
        return risk ? best / SPEED : best;      // minutes in risk mode, metres otherwise
    };

    // 3. Cost of one road in the current mode.
    auto roadCost = [&](int e) { return risk ? g.riskCost(e) : g.edges[e].length; };

    // 4. The search itself.
    const double INF = std::numeric_limits<double>::infinity();
    std::vector<double> cost(n, INF);       // best known cost from the source
    std::vector<int>    parentNode(n, -1);
    std::vector<int>    parentEdge(n, -1);
    std::vector<bool>   settled(n, false);

    typedef std::pair<double, int> Item;    // (priority, node)
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;

    cost[source] = 0;
    open.push({h(source), source});

    int reached = -1;
    while (!open.empty()) {
        int u = open.top().second;
        open.pop();
        if (settled[u]) continue;           // outdated queue entry
        settled[u] = true;
        result.explored++;

        if (isTarget[u]) { reached = u; break; }

        for (const Neighbour& nb : g.adj[u]) {
            if (risk && !g.passable(nb.edgeId)) continue;   // flooded / blocked road: not usable
            double newCost = cost[u] + roadCost(nb.edgeId);
            if (newCost < cost[nb.to]) {
                cost[nb.to] = newCost;
                parentNode[nb.to] = u;
                parentEdge[nb.to] = nb.edgeId;
                open.push({newCost + h(nb.to), nb.to});
            }
        }
    }
    if (reached < 0) return result;         // no safe route exists

    // 5. Walk back from the shelter to the start to read off the route.
    for (int v = reached; v != -1; v = parentNode[v]) {
        result.nodes.push_back(v);
        if (parentEdge[v] != -1) result.edges.push_back(parentEdge[v]);
    }
    std::reverse(result.nodes.begin(), result.nodes.end());
    std::reverse(result.edges.begin(), result.edges.end());

    // 6. Summary numbers for the report (same maths for both modes).
    result.found = true;
    result.destination = reached;
    for (int e : result.edges) {
        result.distance += g.edges[e].length;
        result.minutes  += g.riskCost(e);
        double d = g.edgeDepth(e);
        result.maxDepth = std::max(result.maxDepth, d);
        if (d > 0) result.floodedRoads++;
        if (!g.passable(e)) result.blockedRoads++;
    }
    return result;
}
