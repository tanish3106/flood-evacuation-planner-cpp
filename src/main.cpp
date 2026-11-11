// main.cpp
// ---------------------------------------------------------------------------
// Dynamic Graph-Based Flood Evacuation Route Optimization
//
// This file is the "glue":
//   1. loads the road network from data/network.txt
//   2. starts a small web server on http://localhost:8080
//   3. answers the browser's requests by calling the algorithms:
//        shortest_path.h  -> Dijkstra / A*
//        connectivity.h   -> BFS (cut-off areas) and Tarjan (critical junctions)
//        max_flow.h       -> Edmonds-Karp (how many people can be evacuated)
//   4. sends the answers back as JSON for frontend/app.js to draw.
//
// Build:  g++ -std=c++17 -O2 src/main.cpp -o flood_evac        (Windows: add -lws2_32)
// Run  :  ./flood_evac      then open http://localhost:8080
// ---------------------------------------------------------------------------
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

#include "connectivity.h"
#include "graph.h"
#include "http_server.h"
#include "max_flow.h"
#include "shortest_path.h"

// ============================ tiny JSON helpers =============================
// We build JSON text by hand: it keeps the project free of extra libraries.

static std::string num(double v, int decimals = 2) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(decimals) << v;
    return ss.str();
}

static std::string quoted(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

static std::string boolean(bool b) { return b ? "true" : "false"; }

static std::string intList(const std::vector<int>& v) {
    std::string out = "[";
    for (size_t i = 0; i < v.size(); i++) out += (i ? "," : "") + std::to_string(v[i]);
    return out + "]";
}

static const char* typeName(NodeType t) {
    switch (t) {
        case NodeType::ZONE:     return "zone";
        case NodeType::HOSPITAL: return "hospital";
        case NodeType::SHELTER:  return "shelter";
        default:                 return "junction";
    }
}

// ============================ JSON for each answer ==========================

// The whole network and its current flood state.
static std::string networkJson(const Graph& g) {
    std::string out = "{\"waterLevel\":" + num(g.waterLevel) + ",\"blockDepth\":" + num(BLOCK_DEPTH) + ",\"nodes\":[";
    for (size_t i = 0; i < g.nodes.size(); i++) {
        const Node& n = g.nodes[i];
        out += (i ? "," : "");
        out += "{\"id\":" + std::to_string(n.id) + ",\"name\":" + quoted(n.name) +
               ",\"type\":" + quoted(typeName(n.type)) + ",\"x\":" + num(n.x, 0) + ",\"y\":" + num(n.y, 0) +
               ",\"elevation\":" + num(n.elevation, 1) + ",\"population\":" + std::to_string(n.population) +
               ",\"capacity\":" + std::to_string(n.capacity) + ",\"full\":" + boolean(n.full) +
               ",\"depth\":" + num(g.nodeDepth(n.id)) + "}";
    }
    out += "],\"edges\":[";
    for (size_t i = 0; i < g.edges.size(); i++) {
        const Edge& e = g.edges[i];
        out += (i ? "," : "");
        out += "{\"id\":" + std::to_string(e.id) + ",\"u\":" + std::to_string(e.u) + ",\"v\":" + std::to_string(e.v) +
               ",\"length\":" + num(e.length, 0) + ",\"capacity\":" + std::to_string(e.capacity) +
               ",\"effCapacity\":" + std::to_string(g.effectiveCapacity(e.id)) + ",\"depth\":" + num(g.edgeDepth(e.id)) +
               ",\"blocked\":" + boolean(e.blocked) + ",\"passable\":" + boolean(g.passable(e.id)) + "}";
    }
    return out + "]}";
}

static std::string pathJson(const PathResult& p) {
    if (!p.found) return "{\"found\":false}";
    return "{\"found\":true,\"nodes\":" + intList(p.nodes) + ",\"edges\":" + intList(p.edges) +
           ",\"destination\":" + std::to_string(p.destination) + ",\"distance\":" + num(p.distance, 0) +
           ",\"minutes\":" + num(p.minutes, 1) + ",\"maxDepth\":" + num(p.maxDepth) +
           ",\"floodedRoads\":" + std::to_string(p.floodedRoads) + ",\"blockedRoads\":" + std::to_string(p.blockedRoads) +
           ",\"explored\":" + std::to_string(p.explored) + "}";
}

// Runs every algorithm for the current flood state and returns all results in one JSON object.
static std::string analyzeJson(const Graph& g, int from, bool useAStar) {
    // --- shortest paths -------------------------------------------------------
    PathResult dij   = findPath(g, from, false, CostMode::RISK_AWARE);
    PathResult astar = findPath(g, from, true,  CostMode::RISK_AWARE);
    PathResult naive = findPath(g, from, true,  CostMode::DISTANCE_ONLY);
    const PathResult& chosen = useAStar ? astar : dij;

    std::string out = "{\"route\":" + pathJson(chosen) + ",\"naive\":" + pathJson(naive);
    out += ",\"compare\":{\"dijkstra\":{\"explored\":" + std::to_string(dij.explored) + ",\"found\":" + boolean(dij.found) +
           ",\"minutes\":" + num(dij.minutes, 1) + "},\"astar\":{\"explored\":" + std::to_string(astar.explored) +
           ",\"found\":" + boolean(astar.found) + ",\"minutes\":" + num(astar.minutes, 1) + "}}";

    // --- BFS: places that can no longer reach a shelter --------------------------
    std::vector<bool> reached = reachableFromShelters(g);
    std::vector<int> cutOff;
    for (const Node& n : g.nodes)
        if (n.population > 0 && !reached[n.id]) cutOff.push_back(n.id);
    out += ",\"cutOff\":" + intList(cutOff);

    // --- Tarjan: junctions whose failure would cut people off ----------------------
    out += ",\"critical\":[";
    std::vector<CriticalJunction> crit = findCriticalJunctions(g);
    for (size_t i = 0; i < crit.size(); i++)
        out += (i ? "," : "") + std::string("{\"node\":") + std::to_string(crit[i].node) +
               ",\"stranded\":" + std::to_string(crit[i].newlyStranded) + "}";
    out += "]";

    // --- Edmonds-Karp: how many people can be evacuated in an hour -------------------
    FlowResult f = computeEvacuationFlow(g);
    out += ",\"flow\":{\"total\":" + std::to_string(f.maxFlow) + ",\"people\":" + std::to_string(f.totalPeople) + ",\"places\":[";
    bool first = true;
    for (const Node& n : g.nodes) {
        if (n.population <= 0) continue;
        out += std::string(first ? "" : ",") + "{\"node\":" + std::to_string(n.id) + ",\"people\":" +
               std::to_string(n.population) + ",\"out\":" + std::to_string(f.evacuated[n.id]) + "}";
        first = false;
    }
    out += "],\"roads\":[";
    first = true;
    for (const Edge& e : g.edges) {
        if (f.edgeFlow[e.id] == 0) continue;
        out += std::string(first ? "" : ",") + "{\"edge\":" + std::to_string(e.id) + ",\"flow\":" + std::to_string(f.edgeFlow[e.id]) + "}";
        first = false;
    }
    out += "],\"cutRoads\":" + intList(f.cutRoads) + ",\"fullShelters\":" + intList(f.fullShelters) + "}}";
    return out;
}

// Graphviz export (the visualization named in the synopsis). Render with:  neato -Tpng flood_network.dot -o map.png
static std::string dotExport(const Graph& g, const PathResult& route) {
    std::set<int> onRoute(route.edges.begin(), route.edges.end());
    std::vector<bool> reached = reachableFromShelters(g);
    std::ostringstream o;

    auto label = [](std::string s) { for (char& c : s) if (c == '_') c = ' '; return s; };

    o << "graph FloodNetwork {\n  layout=neato; splines=true;\n"
      << "  node [fontname=\"Helvetica\", fontsize=10, style=filled];\n  edge [fontname=\"Helvetica\", fontsize=9];\n";
    for (const Node& n : g.nodes) {
        std::string shape = "ellipse", fill = "#fff3bf";
        if (n.type == NodeType::SHELTER)        { shape = "box"; fill = n.full ? "#adb5bd" : "#b7e4c7"; }
        else if (n.type == NodeType::HOSPITAL)  { shape = "box"; fill = "#ffc9c9"; }
        else if (n.type == NodeType::JUNCTION)  { shape = "circle"; fill = "#e9ecef"; }
        if (n.population > 0 && !reached[n.id]) fill = "#f4a6a0";            // cut-off place
        o << "  n" << n.id << " [label=" << quoted(label(n.name)) << ", shape=" << shape << ", fillcolor=\"" << fill
          << "\", pos=\"" << num(n.x / 100.0) << "," << num((700 - n.y) / 100.0) << "!\"];\n";
    }
    for (const Edge& e : g.edges) {
        double d = g.edgeDepth(e.id);
        std::string attrs;
        if (onRoute.count(e.id))        attrs = "color=\"#16a34a\", penwidth=4";
        else if (!g.passable(e.id))     attrs = "color=\"#d7301f\", style=dashed, label=\"closed\"";
        else if (d > 0)                 attrs = "color=\"#2b6cb0\", label=\"" + num(d) + " m\"";
        else                            attrs = "color=\"#868e96\"";
        o << "  n" << e.u << " -- n" << e.v << " [" << attrs << "];\n";
    }
    o << "}\n";
    return o.str();
}

// ============================ files and routing =============================

static bool readFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

static int toInt(const std::string& s, int fallback) {
    try { return std::stoi(s); } catch (...) { return fallback; }
}

static double toDouble(const std::string& s, double fallback) {
    try { return std::stod(s); } catch (...) { return fallback; }
}

static Response jsonResponse(const std::string& body) {
    Response r;
    r.contentType = "application/json; charset=utf-8";
    r.body = body;
    return r;
}

static Response handleRequest(Graph& g, const std::string& root, const Request& req) {
    // ---- the web page itself (only these three files are served) ----
    if (req.path == "/" || req.path == "/index.html" || req.path == "/style.css" || req.path == "/app.js") {
        std::string file = req.path == "/" ? "index.html" : req.path.substr(1);
        Response r;
        if (!readFile(root + "frontend/" + file, r.body)) { r.status = 404; r.body = "File not found"; return r; }
        if (file == "index.html") r.contentType = "text/html; charset=utf-8";
        else if (file == "style.css") r.contentType = "text/css; charset=utf-8";
        else r.contentType = "application/javascript; charset=utf-8";
        return r;
    }

    // ---- data the page asks for ----
    if (req.path == "/api/network") return jsonResponse(networkJson(g));

    if (req.path == "/api/analyze")
        return jsonResponse(analyzeJson(g, toInt(req.get("from"), -1), req.get("algo") != "dijkstra"));

    if (req.path == "/api/dot") {
        Response r;
        r.contentType = "text/vnd.graphviz; charset=utf-8";
        r.extraHeaders = "Content-Disposition: attachment; filename=\"flood_network.dot\"\r\n";
        r.body = dotExport(g, findPath(g, toInt(req.get("from"), -1), true, CostMode::RISK_AWARE));
        return r;
    }

    // ---- changes the page asks for (POST) ----
    if (req.method == "POST") {
        if (req.path == "/api/level") {
            g.waterLevel = std::max(0.0, std::min(6.0, toDouble(req.get("value"), g.waterLevel)));
            return jsonResponse("{\"ok\":true}");
        }
        if (req.path == "/api/toggle_road") {
            int id = toInt(req.get("id"), -1);
            if (id >= 0 && id < (int)g.edges.size()) g.edges[id].blocked = !g.edges[id].blocked;
            return jsonResponse("{\"ok\":true}");
        }
        if (req.path == "/api/toggle_shelter") {
            int id = toInt(req.get("id"), -1);
            if (id >= 0 && id < (int)g.nodes.size() && g.nodes[id].type == NodeType::SHELTER)
                g.nodes[id].full = !g.nodes[id].full;
            return jsonResponse("{\"ok\":true}");
        }
        if (req.path == "/api/reset") {
            g.reset();
            return jsonResponse("{\"ok\":true}");
        }
    }

    Response r;
    r.status = 404;
    r.body = "Not found";
    return r;
}

int main(int argc, char** argv) {
    int port = argc > 1 ? toInt(argv[1], 8080) : 8080;

    // Works whether you start the program from the project folder or from a sub-folder such as build/.
    Graph graph;
    std::string root;
    for (const std::string& prefix : {std::string(""), std::string("../")}) {
        if (graph.load(prefix + "data/network.txt")) { root = prefix; break; }
    }
    if (graph.nodes.empty()) {
        std::cerr << "Could not find data/network.txt. Run the program from the project folder.\n";
        return 1;
    }

    std::cout << "Loaded " << graph.nodes.size() << " places and " << graph.edges.size() << " roads.\n"
              << "Open http://localhost:" << port << " in your browser (press Ctrl+C to stop).\n";

    HttpServer server;
    if (!server.run(port, [&](const Request& req) { return handleRequest(graph, root, req); })) {
        std::cerr << "Could not start the server on port " << port << " (is it already in use?).\n";
        return 1;
    }
    return 0;
}
