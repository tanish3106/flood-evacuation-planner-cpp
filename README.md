# Dynamic Graph-Based Flood Evacuation Route Optimization

A C++ project with a simple web frontend. The road network of a flood-prone city is modelled as a
weighted graph. When the water rises, roads get slower, then close, and the program recalculates
safe evacuation routes, cut-off areas, critical junctions and how many people can be moved to
shelters.

All the logic is in C++ (standard library only). The browser page only draws what the C++ program
calculates.

## Run it

You need a C++ compiler (g++) and any web browser. Run these commands **from the project folder**.

| System | Command |
|---|---|
| Linux / macOS | `g++ -std=c++17 -O2 src/main.cpp -o flood_evac` then `./flood_evac` |
| Windows (MinGW / MSYS2 g++, VS Code terminal) | `g++ -std=c++17 -O2 src/main.cpp -o flood_evac.exe -lws2_32` then `flood_evac.exe` |

`build_and_run.sh` and `build_and_run.bat` do the same thing in one step.

Then open **http://localhost:8080** in the browser. Press Ctrl+C in the terminal to stop.
To use another port: `./flood_evac 9000`.

## Using the demo

1. Drag the **flood water level** slider, or press *Start rising water* and watch the map.
2. Pick where people start by clicking a zone or junction (or use the drop-down).
3. Click a road to close it (debris, collapse) and click again to reopen it.
4. Click a shelter to mark it full.
5. Compare **A\*** and **Dijkstra** with the radio buttons.
6. *Download Graphviz (.dot)* saves the current network. To draw it: `neato -Tpng flood_network.dot -o map.png`.

A good story to show: start at Riverside Colony and raise the water from 0 to 2.3 m. The route first
goes south to the Stadium, then switches to the northern shelter when South Chowk floods, and finally
Riverside Colony is cut off.

## Project structure

```
flood-evacuation/
├── data/network.txt        the city: places, roads, ground heights, populations, shelter beds
├── src/
│   ├── graph.h             graph (adjacency list) + the flood model
│   ├── shortest_path.h     Dijkstra and A*
│   ├── connectivity.h      BFS (cut-off areas) and Tarjan (critical junctions)
│   ├── max_flow.h          Edmonds-Karp max-flow and minimum cut
│   ├── http_server.h       tiny web server (sockets), only needed to talk to the browser
│   └── main.cpp            connects everything, builds the JSON answers, Graphviz export
└── frontend/
    ├── index.html          page layout
    ├── style.css           styling
    └── app.js              draws the map and results (no routing logic)
```

To understand the project, read the files in this order: `graph.h`, `shortest_path.h`,
`connectivity.h`, `max_flow.h`. Each is short and commented. `http_server.h` is plumbing and can be
treated as a black box.

## How it works

### 1. The graph

| Graph idea | In this project |
|---|---|
| Vertex | residential zone, hospital, road junction or shelter (`Node`) |
| Edge | a road between two places (`Edge`), two-way |
| Edge weight | **changes with the flood**, see below |
| Representation | weighted adjacency list: `adj[node]` = list of (neighbour, road id) |

The graph is loaded once from `data/network.txt`. Only three things change while the program runs:
the water level, roads closed by the user, and shelters marked full. Every algorithm asks the graph
"is this road passable?" and "what does it cost?", so the algorithms never need to change.

### 2. The flood model (what makes the weights dynamic)

Every place has a ground height. For a given water level:

```
depth of a place  = max(0, water level - ground height)
depth of a road   = the larger depth of its two end points
road is closed    = depth >= 0.5 m, or the user closed it
```

Cost of an open road (minutes):

```
cost = (length / speed) x (1 + 4 x depth / 0.5)
```

A dry road costs just its driving time. A road with almost 0.5 m of water costs 5 times as much.
That is why the route avoids wet roads even before they close. Wet roads also carry fewer people per
hour (up to half) in the max-flow part. The constants are at the top of `graph.h`.

### 3. The algorithms

| Question | Algorithm | File | Time |
|---|---|---|---|
| Safest and fastest way to the nearest open shelter? | **Dijkstra** and **A\*** | `shortest_path.h` | O((V+E) log V) |
| Who can no longer reach any shelter? | **BFS** from all open shelters at once | `connectivity.h` | O(V+E) |
| Which junctions are single points of failure? | **Tarjan** articulation points (DFS) | `connectivity.h` | O(V+E) |
| How many people can reach shelters in one hour, and what limits it? | **Edmonds-Karp** max-flow and **minimum cut** | `max_flow.h` | O(V E²) |

**Dijkstra / A\*.** Dijkstra always expands the cheapest unexplored node. A\* expands the node with
the smallest *cost so far + estimate to go*. The estimate is the straight-line distance to the
nearest open shelter divided by the top speed. It never over-estimates (a road is never shorter than
a straight line and the risk multiplier is at least 1), so A\* still finds the best route but
explores fewer nodes. Dijkstra is just A\* with an estimate of 0, so one function does both. The
search stops at the first open shelter it reaches, which is the nearest one. The panel shows how many
nodes each algorithm had to explore.

**Comparison with a plain map app.** The same function is run with `DISTANCE_ONLY`: cost = road
length, floods ignored, full shelters ignored. The page shows how many closed or flooded roads that
route would cross. This is the problem described in the synopsis.

**BFS.** Start a queue with every open shelter, spread along passable roads. Anything never reached
is cut off. Roads are two-way, so searching from the shelters gives the same answer as searching from
each zone.

**Tarjan.** One DFS stores for each node `disc` (visit time) and `low` (earliest visit time
reachable from its subtree). A non-root node `u` is an articulation point if some child `v` has
`low[v] >= disc[u]`. The root is one if it has two or more DFS children. For each articulation point
the program then re-runs the BFS without that node to count how many people would newly be cut off.
Only junctions that would cut off people are reported as critical.

**Max-flow and minimum cut.** The city becomes a flow network:

```
SOURCE --(population)--> zone/hospital --(road capacity, both ways)--> ... --(free beds)--> SINK
```

The max-flow is the number of people that can be moved in one hour. After the flow is computed, the
nodes still reachable from the source through arcs with spare capacity form one side of the minimum
cut. Roads (or shelters) that cross from that side to the other are completely full and are the
bottlenecks. They are shown in pink on the map.

### 4. How the pieces talk

| Request | Purpose |
|---|---|
| `GET /api/network` | all places and roads with their current water depth and status |
| `GET /api/analyze?from=6&algo=astar` | route, plain route, cut-off places, critical junctions, max-flow |
| `GET /api/dot?from=6` | Graphviz file |
| `POST /api/level?value=1.5` | set water level |
| `POST /api/toggle_road?id=3` | close or reopen a road |
| `POST /api/toggle_shelter?id=2` | mark a shelter full or open |
| `POST /api/reset` | back to dry, nothing closed |

You can try them in the browser, for example `http://localhost:8080/api/network`.

## Changing the data

Edit `data/network.txt` (the format is explained at the top of the file) and restart the program.
Road length is calculated from the coordinates, so you only list the two end points and the capacity.
You can add more places, roads and shelters without touching any C++ code.

## Possible viva questions

**Why a graph?** Places and roads map directly onto vertices and edges, and shortest path, connectivity
and flow are all standard graph problems.

**Why not just the shortest distance?** A short road may be flooded or closed. Weighting by depth
makes the algorithm avoid risky roads and reroute when conditions change.

**Why is A\* correct here?** Its estimate (straight-line distance / top speed) never exceeds the real
remaining cost, so it cannot skip the best route.

**Why BFS and not Dijkstra for cut-off areas?** We only need to know whether a shelter can be
reached, not how far it is, so plain BFS is enough and faster.

**What does an articulation point mean on a road map?** A junction whose failure splits the network.
If people depend on it to reach a shelter, it is a critical junction.

**What does the minimum cut tell the authorities?** Which roads or shelters are completely full. Those
are the places where widening a road or opening a new shelter increases the number of people saved.

**Why is the max-flow split between places "unfair"?** Max-flow maximises the total only. Several
different splits can give the same total, and the algorithm returns one of them.

## Limitations (be honest about these in the presentation)

- The city in `data/network.txt` is a made-up sample, not real OpenStreetMap data. Importing OSM
  would only need a converter that writes the same `NODE` / `EDGE` lines.
- The water level is a single number applied to the whole city using ground heights. It stands in for
  real rainfall and flood-depth data.
- Max-flow is a one-hour snapshot. It does not model traffic building up over several hours.
- The server handles one request at a time and listens on `127.0.0.1` only. It is meant for a local
  demo, not for deployment.
