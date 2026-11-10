"""Queries the exception graph of exa-analyze."""

import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path


def restrict_to_reachable_from_main(graph):
    nodes = graph["nodes"]
    edges = graph["edges"]
    reachable = {node["id"] for node in nodes if node["kind"] == "function" and node["is_reachable_from_main"]}
    reachable |= {
        edge["from"]
        for edge in edges
        if edge["kind"] == "call" and edge["to"] in reachable and nodes[edge["from"]]["kind"] == "unknown_function"
    }

    def is_kept(edge_id):
        edge = edges[edge_id]
        return (edge["from"] if edge["kind"] in ("catch", "uncaught") else edge["to"]) in reachable

    graph["exceptions"] = [exception for exception in graph["exceptions"] if is_kept(exception["throw"])]
    for exception in graph["exceptions"]:
        exception["unwinds"] = [step for step in exception["unwinds"] if is_kept(step["to"])]


def following(exception):
    steps = defaultdict(list)
    for step in exception["unwinds"]:
        steps[step["from"]].append(step["to"])
    return steps


def paths(exception):
    steps = following(exception)
    stack = [[exception["throw"]]]
    while stack:
        path = stack.pop()
        if path[-1] not in steps:
            yield path
            continue
        stack.extend(path + [edge_id] for edge_id in steps[path[-1]] if edge_id not in path)


def location(entry, source):
    if "source_location" in source:
        entry["source_location"] = source["source_location"]
    return entry


def thrown(exception, graph):
    node = graph["nodes"][exception["source"]]
    entry = {"exception": exception["source"]}
    if "thrown_type" in node:
        entry["thrown_type"] = node["thrown_type"]
    return entry


def unwind_path(exception, path, graph):
    entry = thrown(exception, graph)
    entry["unwind_path"] = [graph["edges"][edge_id] for edge_id in path]
    return entry


def query_unwinding(graph, args):
    functions = {
        node["id"] for node in graph["nodes"] if node["kind"] == "function" and node["name"] == args.unwinding
    }
    if not functions:
        sys.exit("{}: no function named {}".format(args.graph, args.unwinding))
    return [
        unwind_path(exception, path, graph)
        for exception in graph["exceptions"]
        for path in paths(exception)
        if any(graph["edges"][edge_id]["to"] in functions for edge_id in path)
    ]


def query_uncaught(graph, args):
    nodes = graph["nodes"]
    functions = defaultdict(list)
    for edge in graph["edges"]:
        if edge["kind"] == "uncaught":
            functions[edge["to"]].append(nodes[edge["from"]])
    exceptions = defaultdict(list)
    for exception in graph["exceptions"]:
        for path in paths(exception):
            edge = graph["edges"][path[-1]]
            if edge["kind"] == "uncaught":
                exceptions[edge["to"]].append(unwind_path(exception, path, graph))
    entries = []
    for node in nodes:
        if node["kind"] not in ("terminate", "undefined_behavior"):
            continue
        if len(functions[node["id"]]) != 1:
            sys.exit("{}: {} node {} has {} uncaught edges".format(args.graph, node["kind"], node["id"], len(functions[node["id"]])))
        function = functions[node["id"]][0]
        if args.reachable_from_main and not function["is_reachable_from_main"]:
            continue
        entry = location({"id": node["id"], "kind": node["kind"], "reason": node["reason"], "function": function["name"]}, function)
        entry["unwind_paths"] = exceptions[node["id"]]
        entries.append(entry)
    return entries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--graph", type=Path, required=True, help="exa_graph.json written by exa-analyze")
    queries = parser.add_argument_group("queries, exactly one").add_mutually_exclusive_group(required=True)
    queries.add_argument(
        "--unwinding",
        metavar="FUNCTION",
        help="unwind paths through FUNCTION, a qualified name such as demo::parser::parsePort",
    )
    queries.add_argument("--uncaught", action="store_true", help="terminate and undefined_behavior nodes")
    parser.add_argument(
        "--reachable-from-main", action="store_true", help="only functions reachable from main"
    )
    args = parser.parse_args()
    graph = json.loads(args.graph.read_text())
    if args.reachable_from_main:
        restrict_to_reachable_from_main(graph)
    query = query_uncaught if args.uncaught else query_unwinding
    json.dump(query(graph, args), sys.stdout, indent=2)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
