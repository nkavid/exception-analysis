ExceptionSummaryInfo = provider(
    doc = "Exception analysis result of a cc_binary.",
    fields = {
        "graph": "Exception graph: nodes, edges, exceptions.",
        "link_unit_summary": "SSAF LU summary the graph is built from.",
        "manifest": "Targets of the binary, their dependency edges, and the TU summary and SSAF compilation unit of each source.",
        "link_coverage": "Static and shared libraries the cc_binary's CppLink action links, as covered, ignored, and uncovered lists. They are its .a and .so inputs and its -l libraries resolved against its -L directories.",
    },
)

def _link_action(target):
    link_actions = [action for action in target.actions if action.mnemonic == "CppLink"]
    if len(link_actions) != 1:
        fail("{}: expected exactly one CppLink action, found {}".format(target.label, len(link_actions)))
    return link_actions[0]

def _link_coverage(ctx, target, analysis_info):
    link_action = _link_action(target)
    link_inputs = link_action.inputs.to_list()
    input_directories = {f.path: f for f in link_inputs if f.is_directory}
    search_directories = [arg[len("-L"):] for arg in link_action.argv if arg.startswith("-L")]
    unknown_directories = [d for d in search_directories if d not in input_directories]
    if unknown_directories:
        fail("{}: CppLink -L directories are not CppLink inputs: {}".format(target.label, unknown_directories))
    library_names = [arg[len("-l"):] for arg in link_action.argv if arg.startswith("-l")]

    covered_link_inputs = ctx.actions.declare_file(ctx.label.name + ".exa_covered_link_inputs.txt")
    ctx.actions.write(covered_link_inputs, "".join([f.path + "\n" for f in analysis_info.covered_link_inputs.to_list()]))

    link_coverage = ctx.actions.declare_file(ctx.label.name + ".exa_link_coverage.json")
    args = ctx.actions.args()
    args.add(str(target.label))
    args.add(link_coverage)
    args.add(covered_link_inputs)
    args.add("--fail-on-uncovered=true")
    args.add_all([f.path for f in link_inputs if not f.is_directory and f.extension in ["a", "so"]])
    args.add_all(search_directories, format_each = "-L%s")
    args.add_all(library_names, format_each = "-l%s")
    args.add_all([f.path for f in ctx.files._known_ignored_link_directories], format_each = "--ignore=%s")
    ctx.actions.run(
        executable = ctx.executable._link_coverage,
        arguments = [args],
        inputs = [covered_link_inputs] + [input_directories[d] for d in search_directories],
        outputs = [link_coverage],
        mnemonic = "ExaTargetLinkCoverage",
        progress_message = "Checking the link coverage of %{label}",
    )
    return link_coverage

def analyze_binary(ctx, target, analysis_info):
    link_coverage = _link_coverage(ctx, target, analysis_info)
    manifest = ctx.actions.declare_file(ctx.label.name + ".exa_manifest.json")
    ctx.actions.write(manifest, json.encode_indent(struct(
        binary = str(target.label),
        targets = analysis_info.targets.to_list(),
    )))

    link_unit_summary = ctx.actions.declare_file(ctx.label.name + ".lu" + ".json")
    link_args = ctx.actions.args()
    link_args.use_param_file("@%s", use_always = False)
    link_args.add_all(analysis_info.translation_unit_summaries)
    link_args.add("-o", link_unit_summary)
    ctx.actions.run(
        executable = ctx.executable._ssaf_linker,
        arguments = [link_args],
        inputs = analysis_info.translation_unit_summaries,
        outputs = [link_unit_summary],
        mnemonic = "ExaSsafLinker",
        progress_message = "Linking SSAF summaries of %{label}",
    )

    graph = ctx.actions.declare_file(ctx.label.name + ".exa_graph.json")
    analyze_args = ctx.actions.args()
    analyze_args.use_param_file("@%s", use_always = False)
    analyze_args.add(link_unit_summary)
    analyze_args.add("--output", graph)
    analyze_args.add(manifest, format = "--manifest=%s")
    analyze_args.add_all(analysis_info.under_analysis_files, format_each = "--under-analysis=%s")
    ctx.actions.run(
        executable = ctx.executable._analyzer,
        arguments = [analyze_args],
        inputs = [link_unit_summary, link_coverage, manifest],
        outputs = [graph],
        mnemonic = "ExaExceptionAnalysisGraph",
        progress_message = "Building the exception graph of %{label}",
    )
    return ExceptionSummaryInfo(graph = graph, link_unit_summary = link_unit_summary, manifest = manifest, link_coverage = link_coverage)
