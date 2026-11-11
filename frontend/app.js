// app.js
// ---------------------------------------------------------------------------
// The browser side. It contains NO routing logic: every number shown here is
// calculated by the C++ program. This file only
//   1. asks the server for the network and the analysis,
//   2. draws them as an SVG map and a results panel,
//   3. sends the user's actions (water level, close road, fill shelter) back.
// ---------------------------------------------------------------------------

const state = {
  net: null,         // from /api/network  : nodes, edges, water level
  result: null,      // from /api/analyze  : route, cut-off places, critical junctions, flow
  start: null,       // id of the place people evacuate from
  algo: 'astar',     // 'astar' or 'dijkstra'
  show: { naive: true, critical: true, bottleneck: true },
};

const $ = (id) => document.getElementById(id);
const pretty = (name) => name.replace(/_/g, ' ');
const SVG_NS = 'http://www.w3.org/2000/svg';

// ---------------------------------------------------------------------------
// Talking to the C++ server
// ---------------------------------------------------------------------------
async function api(path, method = 'GET') {
  const response = await fetch(path, { method });
  if (!response.ok) throw new Error(path + ' returned ' + response.status);
  return response.json();
}

// All actions run one after another, so a quick series of clicks cannot overtake each other.
let queue = Promise.resolve();
function enqueue(task) {
  queue = queue.then(task).catch((err) => showError(err));
  return queue;
}

function showError(err) {
  console.error(err);
  const banner = $('banner');
  banner.textContent = 'Cannot reach the C++ server. Make sure flood_evac is running, then reload this page.';
  banner.hidden = false;
}

async function refresh() {
  const from = state.start === null ? -1 : state.start;
  const [net, result] = await Promise.all([
    api('/api/network'),
    api('/api/analyze?from=' + from + '&algo=' + state.algo),
  ]);
  state.net = net;
  state.result = result;
  $('banner').hidden = true;
  render();
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------
function el(tag, attrs = {}, children = '') {
  const node = document.createElementNS(SVG_NS, tag);
  for (const key in attrs) node.setAttribute(key, attrs[key]);
  if (typeof children === 'string') node.textContent = children;
  return node;
}

function lerp(a, b, t) { return Math.round(a + (b - a) * t); }

// Road colour: grey when dry, light blue to dark blue as water gets deeper.
function roadColor(edge, blockDepth) {
  if (!edge.passable) return '#d64535';
  if (edge.depth <= 0) return '#8a97a6';
  const t = Math.min(1, edge.depth / blockDepth);
  return 'rgb(' + lerp(90, 10, t) + ',' + lerp(169, 63, t) + ',' + lerp(230, 145, t) + ')';
}

function nodeById(id) { return state.net.nodes[id]; }

function roadTitle(edge) {
  const a = pretty(nodeById(edge.u).name), b = pretty(nodeById(edge.v).name);
  let text = a + ' to ' + b + '\n' + edge.length + ' m, capacity ' + edge.effCapacity + ' of ' + edge.capacity + ' people per hour\n';
  text += 'Water depth ' + edge.depth.toFixed(2) + ' m';
  if (edge.blocked) text += '\nClosed by you (click to reopen)';
  else if (!edge.passable) text += '\nClosed by flood water (click to also close it manually)';
  else text += '\nClick to close this road';
  return text;
}

function nodeTitle(node) {
  let text = pretty(node.name) + ' (' + node.type + ')\nGround level ' + node.elevation + ' m, water depth ' + node.depth.toFixed(2) + ' m';
  if (node.population) text += '\nPeople to evacuate: ' + node.population;
  if (node.type === 'shelter') text += '\nBeds: ' + node.capacity + (node.full ? ' (marked full, click to reopen)' : ' (click to mark full)');
  else text += '\nClick to start the evacuation from here';
  return text;
}

// ---------------------------------------------------------------------------
// The map
// ---------------------------------------------------------------------------
function drawMap() {
  const { net, result } = state;
  const svg = $('map');
  svg.replaceChildren();

  // Size the drawing to the data.
  const xs = net.nodes.map((n) => n.x), ys = net.nodes.map((n) => n.y);
  const minX = Math.min(...xs) - 100, maxX = Math.max(...xs) + 130;
  const minY = Math.min(...ys) - 70, maxY = Math.max(...ys) + 100;
  svg.setAttribute('viewBox', minX + ' ' + minY + ' ' + (maxX - minX) + ' ' + (maxY - minY));

  const layerBottle = el('g'), layerRoads = el('g'), layerNaive = el('g'), layerRoute = el('g'),
        layerMarks = el('g'), layerNodes = el('g'), layerLabels = el('g');
  [layerBottle, layerRoads, layerNaive, layerRoute, layerMarks, layerNodes, layerLabels].forEach((g) => svg.appendChild(g));

  const bottlenecks = new Set(result.flow.cutRoads);
  const line = (edge, extra) => {
    const a = nodeById(edge.u), b = nodeById(edge.v);
    return el('line', Object.assign({ x1: a.x, y1: a.y, x2: b.x, y2: b.y, 'stroke-linecap': 'round' }, extra));
  };

  // Roads
  for (const edge of net.edges) {
    const width = 3 + edge.capacity / 120;

    if (state.show.bottleneck && bottlenecks.has(edge.id))
      layerBottle.appendChild(line(edge, { stroke: '#c2185b', 'stroke-width': width + 10, opacity: 0.4 }));

    const road = line(edge, { stroke: roadColor(edge, net.blockDepth), 'stroke-width': width });
    if (!edge.passable) road.setAttribute('stroke-dasharray', '9 8');
    layerRoads.appendChild(road);

    // A wide invisible line makes thin roads easy to click.
    const hit = line(edge, { stroke: 'transparent', 'stroke-width': 18, 'data-edge': edge.id });
    hit.appendChild(el('title', {}, roadTitle(edge)));
    layerRoads.appendChild(hit);

    if (!edge.passable) {
      const a = nodeById(edge.u), b = nodeById(edge.v);
      const mx = (a.x + b.x) / 2, my = (a.y + b.y) / 2, s = 7;
      if (edge.blocked) layerMarks.appendChild(el('circle', { cx: mx, cy: my, r: 11, fill: '#fff', stroke: '#d64535', 'stroke-width': 2.5, 'pointer-events': 'none' }));
      layerMarks.appendChild(el('path', { d: 'M' + (mx - s) + ' ' + (my - s) + 'L' + (mx + s) + ' ' + (my + s) + 'M' + (mx + s) + ' ' + (my - s) + 'L' + (mx - s) + ' ' + (my + s),
        stroke: '#d64535', 'stroke-width': 3.5, 'stroke-linecap': 'round', 'pointer-events': 'none' }));
    }
  }

  // Plain shortest path (dashed amber) and the safe route (solid green with a white edge)
  if (state.show.naive && result.naive.found && !sameEdges(result.naive, result.route)) {
    for (const id of result.naive.edges)
      layerNaive.appendChild(line(net.edges[id], { stroke: '#e69f00', 'stroke-width': 6, 'stroke-dasharray': '3 11', 'pointer-events': 'none' }));
  }
  if (result.route.found) {
    for (const id of result.route.edges) {
      layerRoute.appendChild(line(net.edges[id], { stroke: '#fff', 'stroke-width': 13, 'pointer-events': 'none' }));
    }
    for (const id of result.route.edges) {
      layerRoute.appendChild(line(net.edges[id], { stroke: '#1f9d55', 'stroke-width': 8, 'pointer-events': 'none' }));
    }
  }

  // Places
  const cutOff = new Set(result.cutOff);
  const critical = new Set(result.critical.map((c) => c.node));
  for (const node of net.nodes) {
    drawNode(node, layerNodes, layerLabels, cutOff.has(node.id), state.show.critical && critical.has(node.id));
  }
}

function sameEdges(a, b) {
  return a.found && b.found && a.edges.length === b.edges.length && a.edges.every((id, i) => id === b.edges[i]);
}

function drawNode(node, layerNodes, layerLabels, isCutOff, isCritical) {
  const g = el('g', { 'data-node': node.id });
  g.appendChild(el('title', {}, nodeTitle(node)));
  let radius = 9;      // used for rings and label placement

  // Water around the place
  if (node.depth > 0) {
    const r = node.type === 'junction' ? 17 : 30;
    g.appendChild(el('circle', { cx: node.x, cy: node.y, r, fill: '#1d6fd1', opacity: Math.min(0.45, 0.15 + node.depth / 2) }));
  }

  if (node.type === 'shelter') {
    radius = 24;
    g.appendChild(el('rect', { x: node.x - 20, y: node.y - 20, width: 40, height: 40, rx: 8, fill: node.full ? '#7b8794' : '#1f9d55' }));
    g.appendChild(el('text', { x: node.x, y: node.y + 8, 'text-anchor': 'middle', 'font-size': 22, 'font-weight': 700, fill: '#fff', 'pointer-events': 'none' }, 'S'));
  } else if (node.type === 'hospital') {
    radius = 24;
    g.appendChild(el('rect', { x: node.x - 20, y: node.y - 20, width: 40, height: 40, rx: 8, fill: '#fff', stroke: '#d64535', 'stroke-width': 3 }));
    g.appendChild(el('rect', { x: node.x - 4, y: node.y - 12, width: 8, height: 24, fill: '#d64535', 'pointer-events': 'none' }));
    g.appendChild(el('rect', { x: node.x - 12, y: node.y - 4, width: 24, height: 8, fill: '#d64535', 'pointer-events': 'none' }));
  } else if (node.type === 'zone') {
    radius = 24;
    g.appendChild(el('circle', { cx: node.x, cy: node.y, r: 22, fill: isCutOff ? '#fde3df' : '#fff', stroke: isCutOff ? '#d64535' : '#16222e', 'stroke-width': 3 }));
    g.appendChild(el('text', { x: node.x, y: node.y + 5, 'text-anchor': 'middle', 'font-size': 13, 'font-weight': 700, fill: '#16222e', 'pointer-events': 'none' }, String(node.population)));
  } else {
    g.appendChild(el('circle', { cx: node.x, cy: node.y, r: 9, fill: '#fff', stroke: '#5d6b79', 'stroke-width': 3 }));
  }

  if (isCritical)
    g.appendChild(el('circle', { cx: node.x, cy: node.y, r: radius + 8, fill: 'none', stroke: '#7a3db8', 'stroke-width': 5, 'pointer-events': 'none' }));
  if (node.id === state.start)
    g.appendChild(el('circle', { cx: node.x, cy: node.y, r: radius + 15, fill: 'none', stroke: '#16222e', 'stroke-width': 3, 'stroke-dasharray': '6 5', 'pointer-events': 'none' }));
  layerNodes.appendChild(g);

  // Labels (white outline keeps them readable on top of roads)
  const small = node.type === 'junction';
  const label = el('text', {
    x: node.x, y: node.y + radius + (small ? 18 : 20), 'text-anchor': 'middle',
    'font-size': small ? 14 : 17, 'font-weight': small ? 400 : 600, fill: small ? '#5d6b79' : '#16222e',
    stroke: '#eef3f8', 'stroke-width': 5, 'paint-order': 'stroke', 'pointer-events': 'none',
  }, pretty(node.name));
  layerLabels.appendChild(label);

  const note = node.type === 'shelter' && node.full ? 'full' : isCutOff ? 'cut off' : node.id === state.start ? 'start' : '';
  if (note) {
    layerLabels.appendChild(el('text', {
      x: node.x, y: node.y + radius + 38, 'text-anchor': 'middle', 'font-size': 15, 'font-weight': 700,
      fill: isCutOff ? '#d64535' : node.id === state.start ? '#16222e' : '#5d6b79',
      stroke: '#eef3f8', 'stroke-width': 5, 'paint-order': 'stroke', 'pointer-events': 'none',
    }, note));
  }
}

// ---------------------------------------------------------------------------
// The results panel
// ---------------------------------------------------------------------------
function nameOf(id) { return pretty(nodeById(id).name); }
function roadName(id) { const e = state.net.edges[id]; return nameOf(e.u) + ' to ' + nameOf(e.v); }
function plural(n, word) { return n + ' ' + word + (n === 1 ? '' : 's'); }

function renderRoute() {
  const { result, start } = state;
  const r = result.route;
  const from = start === null ? '' : nameOf(start);
  let html;

  if (start === null) {
    html = '<p class="muted">Pick a start place to see its route.</p>';
  } else if (nodeById(start).type === 'shelter') {
    html = '<p>' + from + ' is already a shelter. Pick a zone, hospital or junction.</p>';
  } else if (!r.found) {
    html = '<p class="answer bad">No safe route.</p><p>' + from + ' cannot reach any open shelter by road right now.</p>';
  } else {
    html = '<p class="answer ok">' + from + ' to ' + nameOf(r.destination) + '</p>' +
      '<dl class="facts">' +
      '<dt>Travel time</dt><dd>' + r.minutes.toFixed(1) + ' min (flood-adjusted)</dd>' +
      '<dt>Distance</dt><dd>' + r.distance + ' m</dd>' +
      '<dt>Deepest water</dt><dd>' + (r.maxDepth > 0 ? r.maxDepth.toFixed(2) + ' m' : 'dry route') + '</dd>' +
      '<dt>Roads used</dt><dd>' + r.edges.length + '</dd>' +
      '</dl>';
    const c = result.compare;
    if (c.dijkstra.found && c.astar.found) {
      html += '<p class="muted">Both algorithms find the same best cost. Dijkstra had to settle ' + plural(c.dijkstra.explored, 'node') +
        ', A* only ' + c.astar.explored + '.</p>';
    }
  }
  $('routeBox').innerHTML = html;
}

function renderNaive() {
  const { result, start, net } = state;
  const n = result.naive, r = result.route;
  let html;

  if (start === null || nodeById(start).type === 'shelter') {
    html = '<p class="muted">Not applicable.</p>';
  } else if (!n.found) {
    html = '<p class="muted">No route at all on the plain map.</p>';
  } else {
    const problems = [];
    if (n.blockedRoads > 0) problems.push('goes through <b>' + plural(n.blockedRoads, 'closed road') + '</b>, which cars cannot use');
    const wet = n.floodedRoads - n.blockedRoads;
    if (wet > 0) problems.push('crosses <b>' + plural(wet, 'flooded road') + '</b> (up to ' + n.maxDepth.toFixed(2) + ' m deep)');
    if (nodeById(n.destination).full) problems.push('ends at <b>' + nameOf(n.destination) + '</b>, which is full');

    if (problems.length === 0 && sameEdges(n, r)) {
      html = '<p class="ok answer">Same route. Nothing on it is flooded yet.</p>';
    } else if (problems.length === 0) {
      html = '<p>The shortest path is ' + n.distance + ' m to ' + nameOf(n.destination) + ', but the safe route is a better choice for the current conditions.</p>';
    } else {
      html = '<p class="bad answer">A distance-only route would be unsafe.</p><p>It would head for ' + nameOf(n.destination) +
        ' (' + n.distance + ' m) and ' + problems.join(', and ') + '.</p>';
    }
  }
  $('naiveBox').innerHTML = html;
}

function renderCut() {
  const { result } = state;
  if (result.cutOff.length === 0) {
    $('cutBox').innerHTML = '<p class="ok answer">Every place can still reach an open shelter.</p>';
    return;
  }
  const people = result.cutOff.reduce((sum, id) => sum + nodeById(id).population, 0);
  $('cutBox').innerHTML = '<p class="bad answer">' + plural(result.cutOff.length, 'place') + ' cut off, ' + people + ' people.</p>' +
    '<ul class="plain">' + result.cutOff.map((id) => '<li>' + nameOf(id) + ' (' + nodeById(id).population + ')</li>').join('') + '</ul>';
}

function renderCritical() {
  const { result } = state;
  if (result.critical.length === 0) {
    $('critBox').innerHTML = '<p class="ok answer">No single junction is a weak point right now.</p>' +
      '<p class="muted">Raise the water to see which junctions become critical.</p>';
    return;
  }
  $('critBox').innerHTML = '<p class="warn answer">If one of these fails, more people are cut off:</p><ul class="plain">' +
    result.critical.map((c) => '<li><b>' + nameOf(c.node) + '</b>: ' + c.stranded + ' people</li>').join('') + '</ul>';
}

function renderFlow() {
  const { result } = state;
  const f = result.flow;
  const percent = f.people ? Math.round((100 * f.total) / f.people) : 0;
  let html = '<p class="answer">' + f.total + ' of ' + f.people + ' people (' + percent + '%)</p>' +
    '<div class="bar"><i style="width:' + percent + '%"></i></div>';

  if (f.total === f.people) {
    html += '<p class="ok">Roads and shelters can handle everyone within the hour.</p>';
  } else {
    const why = [];
    if (f.cutRoads.length) {
      const names = f.cutRoads.slice(0, 4).map(roadName).join('; ');
      why.push('Bottleneck roads (minimum cut): ' + names + (f.cutRoads.length > 4 ? ' and ' + (f.cutRoads.length - 4) + ' more' : '') + '.');
    }
    if (f.fullShelters.length) why.push('Shelter beds are the limit at: ' + f.fullShelters.map(nameOf).join(', ') + '.');
    if (state.result.cutOff.length) why.push('Some people cannot reach any shelter.');
    html += '<p>' + why.join(' ') + '</p>';
  }

  html += '<table class="places"><tr><th>Place</th><th>People</th><th>Out in 1 h</th></tr>' +
    f.places.map((p) => '<tr><td>' + nameOf(p.node) + '</td><td>' + p.people + '</td><td class="' + (p.out < p.people ? 'bad' : 'ok') + '">' + p.out + '</td></tr>').join('') +
    '</table>' +
    '<p class="muted note">Max-flow maximises the total number of people. How they are split between places is one of several equally good answers.</p>';
  $('flowBox').innerHTML = html;
}

function render() {
  const { net } = state;

  // Water gauge text and the map tint
  const closed = net.edges.filter((e) => !e.passable).length;
  $('levelOut').textContent = net.waterLevel.toFixed(1) + ' m';
  $('closedInfo').textContent = closed + ' of ' + net.edges.length + ' roads closed';
  if (document.activeElement !== $('level')) $('level').value = net.waterLevel;
  const t = Math.min(1, net.waterLevel / 3.5);
  $('mapWrap').style.backgroundColor = 'rgb(' + lerp(238, 190, t) + ',' + lerp(243, 215, t) + ',' + lerp(248, 242, t) + ')';

  // Start selector (rebuilt only once)
  const select = $('startSel');
  if (select.options.length === 0) {
    for (const node of net.nodes) {
      if (node.type === 'shelter') continue;
      select.add(new Option(pretty(node.name), node.id));
    }
  }
  if (state.start !== null) select.value = String(state.start);

  $('algoName').textContent = state.algo === 'astar' ? 'A*' : 'Dijkstra';
  $('dotLink').href = '/api/dot?from=' + (state.start === null ? -1 : state.start);

  drawMap();
  renderRoute();
  renderNaive();
  renderCut();
  renderCritical();
  renderFlow();
}

// ---------------------------------------------------------------------------
// User actions
// ---------------------------------------------------------------------------
let pendingLevel = null;
function setLevel(value) {
  pendingLevel = value;       // if the slider moves fast, only the latest value is sent
  $('levelOut').textContent = value.toFixed(1) + ' m';
  return enqueue(async () => {
    if (pendingLevel === null) return;
    const v = pendingLevel;
    pendingLevel = null;
    await api('/api/level?value=' + v, 'POST');
    await refresh();
  });
}

$('level').addEventListener('input', (e) => setLevel(parseFloat(e.target.value)));

$('map').addEventListener('click', (e) => {
  const roadEl = e.target.closest('[data-edge]');
  const nodeEl = e.target.closest('[data-node]');
  if (nodeEl) {
    const id = parseInt(nodeEl.getAttribute('data-node'), 10);
    if (nodeById(id).type === 'shelter') {
      enqueue(async () => { await api('/api/toggle_shelter?id=' + id, 'POST'); await refresh(); });
    } else {
      state.start = id;
      enqueue(refresh);
    }
  } else if (roadEl) {
    const id = parseInt(roadEl.getAttribute('data-edge'), 10);
    enqueue(async () => { await api('/api/toggle_road?id=' + id, 'POST'); await refresh(); });
  }
});

$('startSel').addEventListener('change', (e) => { state.start = parseInt(e.target.value, 10); enqueue(refresh); });

document.querySelectorAll('input[name=algo]').forEach((radio) =>
  radio.addEventListener('change', (e) => { state.algo = e.target.value; enqueue(refresh); }));

[['optNaive', 'naive'], ['optCritical', 'critical'], ['optBottleneck', 'bottleneck']].forEach(([id, key]) =>
  $(id).addEventListener('change', (e) => { state.show[key] = e.target.checked; if (state.net) drawMap(); }));

// "Start rising water" raises the level by 0.1 m every half second so you can watch routes change.
let riseTimer = null;
function stopRising() {
  clearInterval(riseTimer);
  riseTimer = null;
  $('riseBtn').textContent = 'Start rising water';
}
$('riseBtn').addEventListener('click', () => {
  if (riseTimer) { stopRising(); return; }
  if (parseFloat($('level').value) >= parseFloat($('level').max)) setLevel(0);
  $('riseBtn').textContent = 'Pause';
  riseTimer = setInterval(() => {
    const next = Math.round((state.net.waterLevel + 0.1) * 10) / 10;
    if (next > parseFloat($('level').max)) { stopRising(); return; }
    $('level').value = next;
    setLevel(next);
  }, 500);
});

$('resetBtn').addEventListener('click', () => {
  stopRising();
  enqueue(async () => { await api('/api/reset', 'POST'); await refresh(); });
});

// ---------------------------------------------------------------------------
// Start-up: load the network, pick a sensible default start, draw everything.
// ---------------------------------------------------------------------------
enqueue(async () => {
  const net = await api('/api/network');
  const preferred = net.nodes.find((n) => n.name === 'Riverside_Colony') || net.nodes.find((n) => n.type === 'zone');
  state.start = preferred ? preferred.id : null;
  await refresh();
});
