load("@rules_cc//cc:find_cc_toolchain.bzl", "CC_TOOLCHAIN_TYPE")
load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")
load(":analyzer.bzl", "ExceptionSummaryInfo", "analyze_binary")
load(":extractor.bzl", "ExceptionAnalysisInfo", "compilation_unit_id", "extract_translation_unit_summaries", "skipped_sources", "under_analysis_files")

_STATICALLY_LINKED_DEP_ATTRS = ["deps", "implementation_deps"]

_CC_TOOLCHAIN_STATIC_RUNTIME_ATTRS = ["static_runtime_lib", "srcs", "src", "actual", "exports"]

_PROPAGATED_ATTRS = _STATICALLY_LINKED_DEP_ATTRS + _CC_TOOLCHAIN_STATIC_RUNTIME_ATTRS

def _attr_targets(ctx, attr):
    value = getattr(ctx.rule.attr, attr, None)
    if value == None:
        return []
    if type(value) == "list":
        return value
    return [value]

def _exception_analysis_impl(target, ctx):
    deps = [
        dep
        for attr in _PROPAGATED_ATTRS
        for dep in _attr_targets(ctx, attr)
        if type(dep) == "Target" and ExceptionAnalysisInfo in dep
    ]

    if ctx.rule.kind == "cc_binary":
        cc_toolchain = ctx.rule.toolchains[CC_TOOLCHAIN_TYPE]
        if ExceptionAnalysisInfo not in cc_toolchain:
            fail("{}: the exception analysis aspect did not reach its cc_toolchain".format(target.label))
        deps.append(cc_toolchain)

    translation_units = extract_translation_unit_summaries(ctx, target) if CcInfo in target else []
    target_entry = struct(
        label = str(target.label),
        kind = ctx.rule.kind,
        deps = tuple([str(dep.label) for dep in deps]),
        sources = tuple([
            struct(src = tu.src.path, summary = tu.summary.path, compilation_unit = compilation_unit_id(target.label, tu.src))
            for tu in translation_units
        ]),
        skipped_sources = tuple([f.path for f in skipped_sources(ctx)]),
    )

    analysis_info = ExceptionAnalysisInfo(
        targets = depset(
            [target_entry],
            transitive = [dep[ExceptionAnalysisInfo].targets for dep in deps],
        ),
        translation_unit_summaries = depset(
            [tu.summary for tu in translation_units],
            transitive = [dep[ExceptionAnalysisInfo].translation_unit_summaries for dep in deps],
        ),
        under_analysis_files = depset(
            under_analysis_files(ctx),
            transitive = [dep[ExceptionAnalysisInfo].under_analysis_files for dep in deps],
        ),
        covered_link_inputs = depset(
            [output for action in target.actions for output in action.outputs.to_list()],
            transitive = [dep[ExceptionAnalysisInfo].covered_link_inputs for dep in deps] + [
                dynamic_dep[DefaultInfo].files
                for dynamic_dep in getattr(ctx.rule.attr, "dynamic_deps", [])
            ],
        ),
    )

    if ctx.rule.kind != "cc_binary":
        return [analysis_info, OutputGroupInfo(exa_tu_summaries = analysis_info.translation_unit_summaries)]

    summary_info = analyze_binary(ctx, target, analysis_info)

    return [
        analysis_info,
        summary_info,
        OutputGroupInfo(
            exa_tu_summaries = analysis_info.translation_unit_summaries,
            exa_graph = depset([summary_info.manifest, summary_info.link_unit_summary, summary_info.graph, summary_info.link_coverage]),
        ),
    ]

_RUNTIME_STAGE = str(Label("@llvm//toolchain:runtime_stage"))

def _complete_runtime_stage_impl(_settings, _attr):
    return {_RUNTIME_STAGE: "complete"}

_complete_runtime_stage = transition(
    implementation = _complete_runtime_stage_impl,
    inputs = [],
    outputs = [_RUNTIME_STAGE],
)

def _exec_tool_impl(ctx):
    executable = ctx.actions.declare_file(ctx.label.name)
    ctx.actions.symlink(output = executable, target_file = ctx.executable.tool, is_executable = True)
    return [DefaultInfo(
        executable = executable,
        runfiles = ctx.attr.tool[DefaultInfo].default_runfiles,
    )]

exec_tool = rule(
    implementation = _exec_tool_impl,
    doc = "Builds `tool` in the exec configuration of a complete runtime_stage.",
    executable = True,
    attrs = {
        "tool": attr.label(
            mandatory = True,
            executable = True,
            cfg = "exec",
        ),
    },
)

exception_analysis = aspect(
    implementation = _exception_analysis_impl,
    doc = """Extracts one SSAF ExaExceptionFlow TU summary per translation unit over the statically linked closure.

On a cc_binary it links the summaries with clang-ssaf-linker,
builds the exception graph and returns ExceptionSummaryInfo plus the output group exa_graph.

Limitations: dynamic_deps are not followed: code behind a shared library boundary is treated as
unavailable source.
""",
    attr_aspects = _PROPAGATED_ATTRS,
    toolchains_aspects = [str(CC_TOOLCHAIN_TYPE)],
    attrs = {
        "_extractor": attr.label(
            default = Label("//aspect:exa-extract"),
            executable = True,
            cfg = _complete_runtime_stage,
        ),
        "_ssaf_linker": attr.label(
            default = Label("//aspect:clang-ssaf-linker"),
            executable = True,
            cfg = _complete_runtime_stage,
        ),
        "_analyzer": attr.label(
            default = Label("//aspect:exa-analyze"),
            executable = True,
            cfg = _complete_runtime_stage,
        ),
        "_link_coverage": attr.label(
            default = Label("//aspect:link_coverage"),
            executable = True,
            cfg = "exec",
        ),
        "_known_ignored_link_directories": attr.label_list(
            default = [Label("@llvm//runtimes/glibc:glibc_library_search_directory")],
            allow_files = True,
        ),
    },
)

def _exception_graph_test_impl(ctx):
    graph = ctx.attr.binary[ExceptionSummaryInfo].graph
    launcher = ctx.actions.declare_file(ctx.label.name + ".sh")
    ctx.actions.write(
        launcher,
        "#!/usr/bin/env bash\nexec {} {} \"$@\"\n".format(
            ctx.executable.check.short_path,
            graph.short_path,
        ),
        is_executable = True,
    )
    runfiles = ctx.runfiles(files = [graph, ctx.executable.check]).merge(
        ctx.attr.check[DefaultInfo].default_runfiles,
    )
    return [DefaultInfo(executable = launcher, runfiles = runfiles)]

exception_graph_test = rule(
    implementation = _exception_graph_test_impl,
    doc = "Runs `check` with the exception graph that the exception_analysis aspect builds for `binary` as its first argument.",
    test = True,
    attrs = {
        "binary": attr.label(
            mandatory = True,
            aspects = [exception_analysis],
            providers = [ExceptionSummaryInfo],
            doc = "cc_binary whose exception graph is checked.",
        ),
        "check": attr.label(
            mandatory = True,
            executable = True,
            cfg = "target",
            allow_single_file = True,
            doc = "Executable that receives the graph path and exits non-zero on failure.",
        ),
    },
)
