// graph.h
// ---------------------------------------------------------------------------
// The road network as a weighted, undirected graph (adjacency list) plus the
// "flood model" that makes the weights dynamic.
//
//   Vertices (Node) : residential zones, hospitals, road junctions, shelters
//   Edges    (Edge) : roads connecting them
//
// The graph itself never changes while the program runs. What changes is
//   * waterLevel            (one global number, set from the slider)
//   * Edge::blocked         (a road closed by the user, e.g. debris / collapse)
//   * Node::full            (a shelter that has run out of space)
// Every algorithm asks the Graph "is this road passable?" and "what does it
// cost?", so the algorithms never need to be rewritten when conditions change.
// ---------------------------------------------------------------------------
#pragma once

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// ---- Flood model constants -------------------------------------------------
const double BLOCK_DEPTH = 0.5;    // metres of water at which a road becomes impassable
const double SPEED       = 400.0;  // dry-road speed in metres per minute (about 24 km/h)
const double RISK_WEIGHT = 4.0;    // how strongly water depth increases the cost of a road

enum class NodeType { ZONE, JUNCTION, HOSPITAL, SHELTER };

struct Node {
    int         id = 0;
    std::string name;
    NodeType    type = NodeType::JUNCTION;
    double      x = 0, y = 0;        // position in metres (used for drawing and for A*)
    double      elevation = 0;       // height above the reference level, in metres
    int         population = 0;      // people to evacuate (zones, hospital)
    int         capacity = 0;        // beds available (shelters)
    bool        full = false;        // shelter marked as full by the user
};

struct Edge {
    int    id = 0;
    int    u = 0, v = 0;             // the two end points
    double length = 0;               // metres, computed from the coordinates
    int    capacity = 0;             // people per hour on a dry road
    bool   blocked = false;          // closed manually by the user
};

struct Neighbour {                   // one entry of an adjacency list
    int to;                          // node on the other side of the road
    int edgeId;                      // which road leads there
};

class Graph {
public:
    std::vector<Node>                    nodes;
    std::vector<Edge>                    edges;
    std::vector<std::vector<Neighbour>>  adj;      // adj[n] = roads leaving node n
    double                               waterLevel = 0.0;

    // ---- loading -----------------------------------------------------------
    // File format is described at the top of data/network.txt
    bool load(const std::string& filename) {
        std::ifstream in(filename);
        if (!in) return false;

        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ss(line);
            std::string kind;
            ss >> kind;

            if (kind == "NODE") {
                Node n;
                std::string type;
                ss >> n.id >> type >> n.name >> n.x >> n.y >> n.elevation >> n.population >> n.capacity;
                if (type == "ZONE")          n.type = NodeType::ZONE;
                else if (type == "HOSPITAL") n.type = NodeType::HOSPITAL;
                else if (type == "SHELTER")  n.type = NodeType::SHELTER;
                else                         n.type = NodeType::JUNCTION;
                if ((int)nodes.size() <= n.id) nodes.resize(n.id + 1);
                nodes[n.id] = n;
            } else if (kind == "EDGE") {
                Edge e;
                ss >> e.u >> e.v >> e.capacity;
                e.id = (int)edges.size();
                // Road length = straight-line distance between the two ends.
                double dx = nodes[e.u].x - nodes[e.v].x;
                double dy = nodes[e.u].y - nodes[e.v].y;
                e.length = std::sqrt(dx * dx + dy * dy);
                edges.push_back(e);
            }
        }

        adj.assign(nodes.size(), {});
        for (const Edge& e : edges) {
            adj[e.u].push_back({e.v, e.id});
            adj[e.v].push_back({e.u, e.id});
        }
        return !nodes.empty();
    }

    // Put everything back to "dry, nothing closed, no shelter full".
    void reset() {
        waterLevel = 0.0;
        for (Edge& e : edges) e.blocked = false;
        for (Node& n : nodes) n.full = false;
    }

    // ---- flood model -------------------------------------------------------
    // Depth of water at a node: how far the water level is above the ground.
    double nodeDepth(int n) const { return std::max(0.0, waterLevel - nodes[n].elevation); }

    // A road is as wet as its lowest end, so take the deeper of the two ends.
    double edgeDepth(int e) const {
        return std::max(nodeDepth(edges[e].u), nodeDepth(edges[e].v));
    }

    // Can vehicles use this road at all?
    bool passable(int e) const { return !edges[e].blocked && edgeDepth(e) < BLOCK_DEPTH; }

    // Normal driving time on a dry road (minutes).
    double dryMinutes(int e) const { return edges[e].length / SPEED; }

    // The dynamic edge weight used by Dijkstra / A*:
    //   cost = dry time  x  (1 + RISK_WEIGHT x depth / BLOCK_DEPTH)
    // Dry road  -> multiplier 1.   Almost impassable -> multiplier 1 + RISK_WEIGHT.
    double riskMultiplier(int e) const {
        return 1.0 + RISK_WEIGHT * (edgeDepth(e) / BLOCK_DEPTH);
    }
    double riskCost(int e) const { return dryMinutes(e) * riskMultiplier(e); }

    // Wet roads carry fewer people per hour (up to 50% less just before they close).
    int effectiveCapacity(int e) const {
        double factor = 1.0 - 0.5 * (edgeDepth(e) / BLOCK_DEPTH);
        return (int)std::lround(edges[e].capacity * std::max(0.0, factor));
    }

    // ---- helpers -----------------------------------------------------------
    bool isOpenShelter(int n) const { return nodes[n].type == NodeType::SHELTER && !nodes[n].full; }

    double straightLine(int a, int b) const {
        double dx = nodes[a].x - nodes[b].x, dy = nodes[a].y - nodes[b].y;
        return std::sqrt(dx * dx + dy * dy);
    }
};
