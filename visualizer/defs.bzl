load("//aspect:analyzer.bzl", "ExceptionSummaryInfo")
load("//aspect:defs.bzl", "exception_analysis")

def _exception_graph_dot_impl(ctx):
    graph = ctx.attr.binary[ExceptionSummaryInfo].graph
    dot = ctx.actions.declare_file(ctx.label.name + ".dot")
    args = ctx.actions.args()
    args.add(graph)
    args.add("--output", dot)
    ctx.actions.run(
        executable = ctx.executable._visualizer,
        arguments = [args],
        inputs = [graph],
        outputs = [dot],
        mnemonic = "ExaExceptionGraphDot",
        progress_message = "Drawing the exception graph of %{label}",
    )
    return [DefaultInfo(files = depset([dot]))]

exception_graph_dot = rule(
    implementation = _exception_graph_dot_impl,
    doc = "Draws the exception graph of `binary` as Graphviz DOT.",
    attrs = {
        "binary": attr.label(
            mandatory = True,
            aspects = [exception_analysis],
            providers = [ExceptionSummaryInfo],
            doc = "cc_binary whose exception graph is drawn.",
        ),
        "_visualizer": attr.label(
            default = Label(":exa-dot"),
            executable = True,
            cfg = "exec",
        ),
    },
)
