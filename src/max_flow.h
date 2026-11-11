// max_flow.h
// ---------------------------------------------------------------------------
// "How many people can actually be moved to shelters in one hour?"
//
// We turn the city into a flow network:
//
//        SOURCE ──(population)──> zone / hospital
//        road   : capacity = people per hour (smaller when flooded), works both ways
//        shelter ──(free beds)──> SINK
//
// The maximum flow from SOURCE to SINK is the number of people that can be
// evacuated. The MINIMUM CUT is the set of roads (or shelters) that is
// completely full and therefore limits that number: the bottlenecks.
//
// Algorithm: Edmonds-Karp = Ford-Fulkerson where each augmenting path is found
// with BFS (so it is always the path with the fewest roads). Time O(V * E^2).
// ---------------------------------------------------------------------------
#pragma once

#include <algorithm>
#include <queue>
#include <vector>

#include "graph.h"

// A tiny flow network. Every arc is stored next to its reverse arc, so the
// reverse of arc i is always arc i^1 (i XOR 1). That trick makes the residual
// graph easy: pushing f units along arc i does  cap[i] -= f  and  cap[i^1] += f.
class FlowNetwork {
public:
    struct Arc { int to; int cap; };

    explicit FlowNetwork(int nodeCount) : out(nodeCount) {}

    // Adds arc u->v with capacity 'cap' and its reverse arc with capacity 'backCap'.
    // A two-way road uses backCap = cap. Returns the index of the forward arc.
    int addArc(int u, int v, int cap, int backCap = 0) {
        int id = (int)arcs.size();
        arcs.push_back({v, cap});
        out[u].push_back(id);
        arcs.push_back({u, backCap});
        out[v].push_back(id + 1);
        return id;
    }

    int maxFlow(int source, int sink) {
        int total = 0;
        while (true) {
            // BFS in the residual graph: look for any path with spare capacity.
            std::vector<int> viaArc(out.size(), -1);
            std::vector<bool> seen(out.size(), false);
            std::queue<int> q;
            q.push(source);
            seen[source] = true;
            while (!q.empty() && !seen[sink]) {
                int u = q.front();
                q.pop();
                for (int id : out[u]) {
                    int v = arcs[id].to;
                    if (!seen[v] && arcs[id].cap > 0) {
                        seen[v] = true;
                        viaArc[v] = id;
                        q.push(v);
                    }
                }
            }
            if (!seen[sink]) break;                    // no augmenting path left: done

            // The bottleneck is the smallest spare capacity along the path.
            int push = 1 << 30;
            for (int v = sink; v != source; v = arcs[viaArc[v] ^ 1].to)
                push = std::min(push, arcs[viaArc[v]].cap);
            // Send that much flow along the path.
            for (int v = sink; v != source; v = arcs[viaArc[v] ^ 1].to) {
                arcs[viaArc[v]].cap     -= push;
                arcs[viaArc[v] ^ 1].cap += push;
            }
            total += push;
        }
        return total;
    }

    // Nodes still reachable from 'source' through arcs with spare capacity.
    // After max flow, this set is one side of the minimum cut.
    std::vector<bool> reachableFrom(int source) const {
        std::vector<bool> seen(out.size(), false);
        std::queue<int> q;
        q.push(source);
        seen[source] = true;
        while (!q.empty()) {
            int u = q.front();
            q.pop();
            for (int id : out[u])
                if (!seen[arcs[id].to] && arcs[id].cap > 0) {
                    seen[arcs[id].to] = true;
                    q.push(arcs[id].to);
                }
        }
        return seen;
    }

    std::vector<Arc>              arcs;
    std::vector<std::vector<int>> out;
};

struct FlowResult {
    int              maxFlow = 0;        // people that can reach a shelter in one hour
    int              totalPeople = 0;    // people who need to be evacuated
    std::vector<int> evacuated;          // per node: how many of its people get out
    std::vector<int> edgeFlow;           // per road: net people per hour from u to v (negative = v to u)
    std::vector<int> cutRoads;           // roads in the minimum cut (bottlenecks)
    std::vector<int> fullShelters;       // shelters whose beds are the limit
};

inline FlowResult computeEvacuationFlow(const Graph& g) {
    const int n = (int)g.nodes.size();
    const int SOURCE = n, SINK = n + 1;
    FlowNetwork net(n + 2);

    FlowResult res;
    res.evacuated.assign(n, 0);
    res.edgeFlow.assign(g.edges.size(), 0);

    // Roads (only the ones water has not closed). Remember each road's arc index.
    std::vector<int> roadArc(g.edges.size(), -1);
    for (const Edge& e : g.edges) {
        if (!g.passable(e.id)) continue;
        int cap = g.effectiveCapacity(e.id);
        roadArc[e.id] = net.addArc(e.u, e.v, cap, cap);
    }

    // Source -> every place that has people to evacuate.
    std::vector<int> sourceArc(n, -1), shelterArc(n, -1);
    for (int i = 0; i < n; i++) {
        if (g.nodes[i].population > 0) {
            sourceArc[i] = net.addArc(SOURCE, i, g.nodes[i].population);
            res.totalPeople += g.nodes[i].population;
        }
        if (g.isOpenShelter(i)) shelterArc[i] = net.addArc(i, SINK, g.nodes[i].capacity);
    }

    res.maxFlow = net.maxFlow(SOURCE, SINK);

    // How many people left each place = what was taken from its source arc.
    for (int i = 0; i < n; i++)
        if (sourceArc[i] != -1)
            res.evacuated[i] = g.nodes[i].population - net.arcs[sourceArc[i]].cap;

    // Flow on each road = capacity that was used up.
    for (const Edge& e : g.edges)
        if (roadArc[e.id] != -1)
            res.edgeFlow[e.id] = g.effectiveCapacity(e.id) - net.arcs[roadArc[e.id]].cap;

    // Minimum cut: roads that connect the "still reachable" side to the "full" side.
    std::vector<bool> side = net.reachableFrom(SOURCE);
    for (const Edge& e : g.edges)
        if (roadArc[e.id] != -1 && side[e.u] != side[e.v]) res.cutRoads.push_back(e.id);
    for (int i = 0; i < n; i++)
        if (shelterArc[i] != -1 && side[i]) res.fullShelters.push_back(i);

    return res;
}
