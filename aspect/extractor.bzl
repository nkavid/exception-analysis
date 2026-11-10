load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")

ExceptionAnalysisInfo = provider(
    doc = "Exception analysis input of a target and its static link closure.",
    fields = {
        "translation_unit_summaries": "SSAF TU summaries, linked into the cc_binary's link unit summary.",
        "targets": "Populates the manifest.",
        "under_analysis_files": "Sources and headers of targets tagged exa_under_analysis.",
        "covered_link_inputs": "Files produced by the actions of the static link closure, plus the files of its dynamic_deps.",
    },
)

_UNDER_ANALYSIS_TAG = "exa_under_analysis"

def _path_inside_own_output_tree(short_path):
    return short_path.replace("../", "external/")

def _source_directory(ctx, src):
    return "{}.exa/{}".format(ctx.label.name, _path_inside_own_output_tree(src.short_path))

def _compile_action(target, src):
    for action in target.actions:
        if action.mnemonic == "CppCompile" and src.path in action.argv:
            return action
    fail("{}: no CppCompile action compiles {}".format(target.label, src.path))

def compilation_unit_id(label, src):
    return "{}#{}".format(label, src.short_path)

_CXX_SOURCE_EXTENSIONS = ["cc", "cpp", "c"]

_CXX_HEADER_EXTENSIONS = ["hpp", "hh", "h", "ipp"]

_ASSEMBLY_EXTENSIONS = ["S"]

def _cxx_sources(ctx):
    srcs = getattr(ctx.rule.files, "srcs", [])
    unsupported = [f.path for f in srcs if f.extension not in _CXX_SOURCE_EXTENSIONS + _CXX_HEADER_EXTENSIONS + _ASSEMBLY_EXTENSIONS]
    if unsupported:
        fail("{}: srcs {} are neither {} sources, {} headers nor {} assembly".format(ctx.label, unsupported, _CXX_SOURCE_EXTENSIONS, _CXX_HEADER_EXTENSIONS, _ASSEMBLY_EXTENSIONS))
    return [f for f in srcs if f.extension in _CXX_SOURCE_EXTENSIONS]

def skipped_sources(ctx):
    return [f for f in getattr(ctx.rule.files, "srcs", []) if f.extension in _ASSEMBLY_EXTENSIONS]

def extract_translation_unit_summaries(ctx, target):
    srcs = _cxx_sources(ctx)
    if not srcs:
        return []

    translation_units = []
    for src in srcs:
        compile_action = _compile_action(target, src)
        summary = ctx.actions.declare_file(_source_directory(ctx, src) + "/summary.json")
        args = ctx.actions.args()
        args.add("--extra-arg=--ssaf-extract-summaries=ExaExceptionFlow,PointerFlow")
        args.add(summary, format = "--extra-arg=--ssaf-tu-summary-file=%s")
        args.add(compilation_unit_id(target.label, src), format = "--extra-arg=--ssaf-compilation-unit-id=%s")
        args.add("--extra-arg=--ssaf-include-local-entities")
        args.add("--extra-arg=-Werror=scalable-static-analysis-framework")
        args.add(src)
        args.add("--")
        args.add_all(compile_action.argv[1:])
        ctx.actions.run(
            executable = ctx.executable._extractor,
            arguments = [args],
            inputs = depset(transitive = [
                compile_action.inputs,
                target[CcInfo].compilation_context.headers,
            ] + [
                dep[CcInfo].compilation_context.headers
                for dep in getattr(ctx.rule.attr, "implementation_deps", [])
            ]),
            outputs = [summary],
            mnemonic = "ExaSsafTuSummary",
            progress_message = "Extracting SSAF ExaExceptionFlow summary of " + src.short_path,
        )
        translation_units.append(struct(src = src, summary = summary))
    return translation_units

def under_analysis_files(ctx):
    if _UNDER_ANALYSIS_TAG not in ctx.rule.attr.tags:
        return []
    return [
        f
        for attr in ["srcs", "hdrs", "textual_hdrs"]
        for f in getattr(ctx.rule.files, attr, [])
    ]
