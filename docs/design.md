# Design

## Goal

Given one linked executable answer **which exceptions can be thrown, which
functions they unwind through, and where they are caught or not caught.**

Use case is the
[AUTOSAR C++14 exception rules](autosar.md).
Per-TU checks (`-Wexceptions`, `bugprone-exception-escape`) are out of scope.

## Assumptions

| Topic | Decision |
|---|---|
| Unit of analysis | `cc_binary` and its static link closure, including `implementation_deps` and `cc_toolchain` runtime libraries |
| Link coverage | Every linked library comes from the closure, `dynamic_deps`, or glibc. Otherwise the build fails. |
| `dynamic_deps` | Not analysed. Their functions have no source. |
| No source | A function without source and without `nounwind_cause` throws an *unknown exception*. |
| Function pointers | Resolved with SSAF PointerFlow. Unresolved callees are `unknown_function`. |
| `catch(...)` | The only handler of an *unknown exception*. |
| Virtual calls | Reach every overrider in the binary. |
| ODR | The first definition is used. |
| Platform | Linux, Itanium C++ ABI. |
| Toolchain | Clang. Builds by other toolchains, such as GCC, are future work. |

## Exception specification and `nounwind`

Both are read from clang as it compiles the translation unit. The analysis
assumes nothing about `extern "C"`.

`exception_specification` is `non_throwing` or `potentially_throwing`, as
defined in [except.spec]. It is absent for functions compiled as C.

`nounwind_cause` is why clang marks the function LLVM
[`nounwind`](https://llvm.org/docs/LangRef.html#function-attributes). An
exception propagates out of a function without `nounwind_cause`.

| `nounwind_cause` | Clang input | Exception leaving the function |
|---|---|---|
| `exception_specification` | non-throwing exception specification | `terminate` with reason `noexcept`, [except.terminate] |
| `nothrow_attribute` | `__attribute__((nothrow))`, also implicit on `extern "C"` functions under the cc1 option [`-fexternc-nounwind`](https://github.com/llvm/llvm-project/blob/main/clang/include/clang/Options/Options.td) | `undefined_behavior` with reason `nounwind` |
| `exceptions_disabled` | `-fno-exceptions`, the default for C | `undefined_behavior` with reason `nounwind` |
| `cxx_exceptions_disabled` | non-throwing exception specification under `-fexceptions -fno-cxx-exceptions` | `undefined_behavior` with reason `nounwind` |

Clang emits the `std::terminate` call only for the first row. Unwinding out of
a `nounwind` function is undefined behaviour. `exceptions_disabled` applies
even with a non-throwing exception specification.

[except.spec]: https://eel.is/c++draft/except.spec
[except.terminate]: https://eel.is/c++draft/except.terminate

## Architecture

SSAF (`clang/ScalableStaticAnalysis`) extracts, links, and analyses summaries.
This project adds the Bazel aspect that defines the link unit, and the
`ExaExceptionFlow` summary and analysis.

```
  Bazel aspect over cc_binary + deps, aspect/defs.bzl
 ┌──────────── per translation unit ─────────────────────────────┐
 │ exa-extract        SSAF TUSummaryExtractorFrontendAction      │ ──► <src>/summary.json  (TU summary)
 │                    on the target's CppCompile argv            │
 │                    + ExaExceptionFlow, PointerFlow extractors │
 └───────────────────────────────────────────────────────────────┘
 ┌──────────── per cc_binary ────────────────────────────────────┐
 │ manifest           targets, dep edges, TU → summary           │ ──► <bin>.exa_manifest.json
 │ coverage check     CppLink -l/-L against the closure          │ ──► <bin>.exa_link_coverage.json
 │ clang-ssaf-linker  upstream, schema agnostic                  │ ──► <bin>.lu.json  (LU summary)
 │ exa-analyze        SSAF AnalysisDriver + graph JSON           │ ──► <bin>.exa_graph.json
 └───────────────────────────────────────────────────────────────┘
```

The JSON files are the interfaces between the Bazel actions. Users get a
queryable database built from them, so the writer and the reader of each
file must parse the same format.

| Directory | Contents |
|---|---|
| `aspect/` | `exception_analysis` aspect and test rule in `defs.bzl`, TU summary extraction and `ExceptionAnalysisInfo` in `extractor.bzl`, manifest, link coverage check, SSAF linking, graph, and `ExceptionSummaryInfo` in `analyzer.bzl` |
| `rule/` | C++ compile options |
| `exception_flow/summary/` | `ExaExceptionFlow` summary and its JSON format |
| `exception_flow/summary/test/` | serialize and deserialize tests against `test_data/`, stream operators for test output |
| `exception_flow/extractor/` | `ExaExceptionFlow` extractor, `exa-extract` |
| `exception_flow/extractor/test/` | unit tests, code in and summary facts out |
| `exception_flow/analyzer/` | analyses, border crossing, graph JSON, `exa-analyze` |
| `visualizer/` | `exa-dot` and its `exception_graph_dot` rule, unwind paths through code under analysis and reachable from `main` as Graphviz DOT, library frames before code under analysis collapsed |
| `query/` | `exa-query`, unwind paths through a function or into each `uncaught` edge |

## Exception graph

Three tables. Ids are positions in their table.

**Nodes**

| Kind | Role | Attributes |
|---|---|---|
| `function` | function of the link unit | `name`, `is_defined`, `exception_specification`, `nounwind_cause`, `language_linkage`, `is_entry_point`, `is_reachable_from_main`, `in_system_header`, `is_under_analysis`, `source_location` |
| `exception` | one per throw site, or one *unknown exception* per throwing callee without source | `thrown_type`, absent for an unknown exception |
| `catch` | handler | `caught_type`, absent for `catch(...)` |
| `terminate` | `std::terminate` is called, [except.terminate]. One per function and `reason`. | `reason` |
| `undefined_behavior` | unwinding out of a `nounwind` function without a `std::terminate` call. One per function and `reason`. | `reason` is `nounwind` |
| `unknown_function` | unresolved indirect callee | `function_type`, `exception_specification`, `nounwind_cause` |

`exception_specification` and `nounwind_cause` are described in
[Exception specification and `nounwind`](#exception-specification-and-nounwind).
An `unknown_function` takes them from `function_type`. `language_linkage` is
`c` or `cxx`, [dcl.link].

`is_entry_point` is true for `main` only, [basic.start.main]. Other ways into
the program are not entry points yet. `is_reachable_from_main` is true when a
path of `call` edges leads from `main` to the function.

[basic.start.main]: https://eel.is/c++draft/basic.start.main

| `terminate` reason | Uncaught where |
|---|---|
| `no_handler` | exception leaves an entry point |
| `noexcept` | exception leaves a function with `nounwind_cause` `exception_specification` |
| `no_currently_handled_exception` | `throw;` is reached from an entry point with no handler active |

**Edges**

| Kind | From → to | Attributes |
|---|---|---|
| `throw` | `exception` → `function` or `unknown_function` | `source_location` |
| `rethrow` | caught `exception` → `function` containing `throw;` | `source_location` |
| `catch` | `function` → `catch` | `source_location` |
| `call` | callee `function` or `unknown_function` → caller `function` | `source_location` |
| `uncaught` | `function` → `terminate` or `undefined_behavior` | |

An `uncaught` edge is where unwinding stops without a handler. The target node
is the outcome.

**Border crossing.** Every edge has a `crossing` object. Each node belongs to a
function. A `function` or `unknown_function` belongs to itself. An `exception`
belongs to the function it is thrown into. A `catch`, `terminate`, or
`undefined_behavior` node belongs to the function it is attached to. A property
holds `from` and `to` of the two functions and is present only when both are
known and differ.

| Property | Value |
|---|---|
| `translation_unit` | main file of the [translation unit](https://eel.is/c++draft/lex.phases) |
| `cc_target` | Bazel label of the target compiling that source |
| `language_linkage` | as on `function` |
| `exception_specification` | as on `function` |
| `nounwind` | `true` when the function has a `nounwind_cause` |

Functions without source have no `translation_unit` or `cc_target`. Functions
without external linkage have no `language_linkage`.

[dcl.link]: https://eel.is/c++draft/dcl.link

**Exceptions** has per `exception` node its `source`, its `throw` edge, and the
steps it `unwinds` along. A step is a pair of edge ids, `from` and `to`. The
exception continues along `to` after `from`. The `uncaught` edge of reason
`no_currently_handled_exception` belongs to no exception.

| `from` | `to` |
|---|---|
| `throw`, `call`, or `rethrow` into a function | the `catch` edge of the first matching handler at that site |
| `catch` | each `rethrow` edge of that handler |
| `throw`, `call`, or `rethrow` into a function without a matching handler | each `call` edge to a caller, or the `uncaught` edge of that function |

Every path of steps from the `throw` edge is a way the exception unwinds. A path
ends at a `catch` edge without `rethrow` steps or at an `uncaught` edge.

## Known limitations of v0

* Path insensitive.
* Implicit member and base destructor calls in destructors, `catch` parameter
  initialization, coroutines, and constructor function-try-blocks are not
  modelled.
* Implicitly defined special members have no `function` node. SSAF gives
  implicit declarations no entity. Their calls are attributed to the function
  that uses them.
* Implicit declarations of global `operator new` and `operator delete` have no
  SSAF entity. Calls to them throw an unknown exception.
* Exception specifications clang never evaluated are potentially-throwing,
  except for destructors, which are non-throwing.
* An exception leaving a function that is never called in the binary has no
  `catch` or `uncaught` edge after leaving it.
* `no_currently_handled_exception` considers only call paths from entry points.
* Initialization and destruction of non-block variables with static or thread
  storage duration are not extracted. Modelling them needs a verification
  strategy with test coverage first.
* `main` is the only entry point. Functions entered otherwise are not reachable
  from main. These are initialization and destruction of variables with static
  or thread storage duration, callbacks called by functions without source,
  such as `atexit`, `signal`, and `pthread_create`, and callees reached only
  through an `unknown_function`.
