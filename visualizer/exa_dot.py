"""Writes the exception graph of exa-analyze as Graphviz DOT."""

import argparse
import json
from collections import defaultdict
from pathlib import Path

NODE_STYLES = {
    "function": 'shape=box style=rounded',
    "exception": 'shape=box color=red style=bold',
    "unknown_exception": 'shape=box color=red style="bold,dashed"',
    "catch": 'shape=house peripheries=2 color=darkgreen',
    "terminate": 'shape=doubleoctagon margin=0 color=red',
    "undefined_behavior": 'shape=doubleoctagon margin=0 color=red style=dashed',
    "unknown_function": 'shape=diamond color=orange',
}

LEGEND_LABELS = {
    "undefined_behavior": "undefined behavior",
}

COLLAPSE_OUTSIDE_CALLEES = True

COLLAPSED = "collapsed"

EDGE_STYLES = {
    COLLAPSED: 'color=gray style=dashed',
    "throw": 'color=red',
    "rethrow": 'color=red style=dashed',
    "catch": 'color=darkgreen',
    "call": '',
    "uncaught": 'color=red style=bold',
}


def node_label(node):
    kind = node["kind"]
    if kind == "function":
        return node["name"]
    if kind == "exception":
        return node.get("thrown_type", "unknown exception")
    if kind == "catch":
        return "catch ({})".format(node.get("caught_type", "..."))
    if kind == "unknown_function":
        return node["function_type"]
    if kind in ("terminate", "undefined_behavior"):
        return "{}\n({})".format(LEGEND_LABELS.get(kind, kind), node["reason"])
    return kind


def node_style(node):
    if node["kind"] == "exception" and "thrown_type" not in node:
        return NODE_STYLES["unknown_exception"]
    return NODE_STYLES[node["kind"]]


def is_first_party(node):
    return node["kind"] == "function" and node["is_under_analysis"]


def reachable(starts, successors):
    seen = set(starts)
    pending = list(starts)
    while pending:
        for successor in successors.get(pending.pop(), []):
            if successor not in seen:
                seen.add(successor)
                pending.append(successor)
    return seen


def reachable_from_main(graph):
    nodes = graph["nodes"]
    functions = {node["id"] for node in nodes if node["kind"] == "function" and node["is_reachable_from_main"]}
    functions |= {
        edge["from"]
        for edge in graph["edges"]
        if edge["kind"] == "call" and edge["to"] in functions and nodes[edge["from"]]["kind"] == "unknown_function"
    }
    return {
        edge["id"]
        for edge in graph["edges"]
        if (edge["from"] if edge["kind"] in ("catch", "uncaught") else edge["to"]) in functions
    }


def edges_through_first_party(exception, graph, drawable):
    nodes = graph["nodes"]
    edges = graph["edges"]
    steps = [step for step in exception["unwinds"] if step["from"] in drawable and step["to"] in drawable]
    forward = defaultdict(list)
    backward = defaultdict(list)
    for step in steps:
        forward[step["from"]].append(step["to"])
        backward[step["to"]].append(step["from"])
    unwound = {exception["throw"]} | {step["to"] for step in steps}
    entering = {edge_id for edge_id in unwound & drawable if is_first_party(nodes[edges[edge_id]["to"]])}
    after_entering = reachable(entering, forward)
    before_entering = reachable(entering, backward)
    if not COLLAPSE_OUTSIDE_CALLEES:
        return [edges[edge_id] for edge_id in after_entering | before_entering], []
    outside_callees = {
        edges[edge_id]["from"]
        for edge_id in entering
        if edges[edge_id]["kind"] == "call" and not is_first_party(nodes[edges[edge_id]["from"]])
    }
    collapsed = [{"from": exception["source"], "to": callee, "kind": COLLAPSED} for callee in outside_callees]
    return [edges[edge_id] for edge_id in after_entering], collapsed


def selected_edges(graph):
    drawable = reachable_from_main(graph)
    kept = {}
    collapsed = {}
    for exception in graph["exceptions"]:
        exception_kept, exception_collapsed = edges_through_first_party(exception, graph, drawable)
        kept.update((edge["id"], edge) for edge in exception_kept)
        collapsed.update(((edge["from"], edge["to"]), edge) for edge in exception_collapsed)
    unwound = {
        edge_id
        for exception in graph["exceptions"]
        for edge_id in [exception["throw"]] + [step["to"] for step in exception["unwinds"]]
    }
    kept.update(
        (edge["id"], edge)
        for edge in graph["edges"]
        if edge["kind"] == "uncaught"
        and edge["id"] not in unwound
        and edge["id"] in drawable
        and is_first_party(graph["nodes"][edge["from"]])
    )
    return [kept[edge_id] for edge_id in sorted(kept)] + list(collapsed.values())


def dot(graph):
    nodes = graph["nodes"]
    edges = selected_edges(graph)
    drawn = sorted({edge["from"] for edge in edges} | {edge["to"] for edge in edges})

    lines = ["digraph exceptions {", "  newrank=true;", "  node [fontname=monospace fontsize=10];"]
    lines.append('  subgraph cluster_legend { label="legend"; rank=source;')
    lines.extend(
        '  legend_{} [label="{}" {}];'.format(kind, LEGEND_LABELS.get(kind, kind), style)
        for kind, style in NODE_STYLES.items()
    )
    lines.append("  }")
    targets = {edge["to"] for edge in edges}
    lines.extend(
        "  legend_{} -> n{} [style=invis weight=0];".format(next(iter(NODE_STYLES)), node_id)
        for node_id in drawn
        if node_id not in targets
    )
    lines.extend(
        "  n{} [label={} {}];".format(node_id, json.dumps(node_label(nodes[node_id])), node_style(nodes[node_id]))
        for node_id in drawn
    )
    lines.extend(
        "  n{} -> n{} [label={} {}];".format(
            edge["from"], edge["to"], json.dumps(edge["kind"]), EDGE_STYLES[edge["kind"]]
        )
        for edge in edges
    )
    lines.append("}")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("graph", type=Path, help="exa_graph.json written by exa-analyze")
    parser.add_argument("--output", type=Path, required=True, help="DOT file to write")
    args = parser.parse_args()
    graph = json.loads(args.graph.read_text())
    args.output.write_text(dot(graph))


if __name__ == "__main__":
    main()
