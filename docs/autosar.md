# AUTOSAR C++14 exception rules

Quoted from
[AUTOSAR C++14 Guidelines](https://www.autosar.org/fileadmin/standards/R22-11/AP/AUTOSAR_RS_CPP14Guidelines.pdf),
document ID 839, release 19-03, chapter 6.15. Classification is AUTOSAR's.

## Whole-program rules

| Rule | Requirement | AUTOSAR | This project |
|---|---|---|---|
| A15-4-2 | If a function is declared to be noexcept, noexcept(true) or noexcept(\<true condition\>), then it shall not exit with an exception. | automated | `terminate` node with reason `noexcept` |
| A15-5-3 | The std::terminate() function shall not be called implicitly. | automated | `terminate` node |
| A15-5-2 | Program shall not be abruptly terminated. In particular, an implicit or explicit invocation of std::abort(), std::quick_exit(), std::_Exit(), std::terminate() shall not be done. | partially automated | `terminate` node for termination by exception handling, explicit calls are not exception flow |
| M15-3-4 | Each exception explicitly thrown in the code shall have a handler of a compatible type in all call paths that could lead to that point. | automated | `terminate` node with reason `no_handler` |
| A15-4-4 | A declaration of non-throwing function shall contain noexcept specification. | automated | `function` with `exception_specification` `potentially_throwing` that no exception unwinds through |
| A15-4-3 | The noexcept specification of a function shall either be identical across all translation units, or identical or more restrictive between a virtual member function and an overrider. | automated | planned |
| A15-4-5 | Checked exceptions that could be thrown from a function shall be specified together with the function declaration and they shall be identical in all function declarations and for all its overriders. | automated | exceptions unwinding out of each function and call can be enumerated from the graph |
| A15-5-1 | All user-provided class destructors, deallocation functions, move constructors, move assignment operators and swap functions shall not exit with an exception. A noexcept exception specification shall be added to these functions as appropriate. | automated | planned |
| A15-2-1 | Constructors that are not noexcept shall not be invoked before program startup. | automated | planned |
| M15-3-1 | Exceptions shall be raised only after start-up and before termination. | automated | planned |
| A15-1-4 | If a function exits with an exception, then before a throw, the function shall place all objects/resources that the function constructed in valid states or it shall delete them. | partially automated | planned |
| A15-2-2 | If a constructor is not noexcept and the constructor cannot finish object initialization, then it shall deallocate the object's resources and it shall throw an exception. | partially automated | planned |
| A15-1-3 | All thrown exceptions should be unique. | automated | planned, throw sites per exception type, separating throws from rethrows and by constructor arguments |

## Rules classified not automatable

| Rule | Requirement | AUTOSAR | This project |
|---|---|---|---|
| A15-1-5 | Exceptions shall not be thrown across execution boundaries. | non-automated | planned, boundaries are in the build graph |
| A15-3-2 | If a function throws an exception, it shall be handled when meaningful actions can be taken, otherwise it shall be propagated. | non-automated | `catch` edges per handler |
| A15-3-3 | Main function and a task main function shall catch at least: base class exceptions from all third-party libraries used, std::exception and all otherwise unhandled exceptions. | partially automated | `terminate` node with reason `no_handler` from `main` |
| A15-3-4 | Catch-all (ellipsis and std::exception) handlers shall be used only in (a) main, (b) task main functions, (c) in functions that are supposed to isolate independent components and (d) when calling third-party code that uses exceptions not according to AUTOSAR C++14 guidelines. | non-automated | planned |
| A15-0-3 | Exception safety guarantee of a called function shall be considered. | non-automated | exceptions escaping the called function |

## Out of scope

### Per TU

These rules can be decided from one translation unit. The compiler and
clang-tidy cover part of them, and the exception graph adds nothing. AUTOSAR
gives the M rules only as a headline that refers to MISRA C++ 2008.

| Rule | Requirement | Covered by | Not covered |
|---|---|---|---|
| A15-1-1 | Only instances of types derived from std::exception should be thrown. | [`bugprone-std-exception-baseclass`](https://clang.llvm.org/extra/clang-tidy/checks/bugprone/std-exception-baseclass.html) | Direct instances of `std::exception`, which the rationale forbids and the check accepts |
| A15-1-2 | An exception object shall not be a pointer. | [`misc-throw-by-value-catch-by-reference`](https://clang.llvm.org/extra/clang-tidy/checks/misc/throw-by-value-catch-by-reference.html) | String literals, which the check exempts |
| A15-3-5 | A class type exception shall be caught by reference or const reference. | [`misc-throw-by-value-catch-by-reference`](https://clang.llvm.org/extra/clang-tidy/checks/misc/throw-by-value-catch-by-reference.html) | Trivial class types, which the check flags only with `WarnOnLargeObject` and above `MaxSize` |
| A15-4-1 | Dynamic exception-specification shall not be used. | [`modernize-use-noexcept`](https://clang.llvm.org/extra/clang-tidy/checks/modernize/use-noexcept.html), [`-Wdeprecated-dynamic-exception-spec`](https://clang.llvm.org/docs/DiagnosticsReference.html#wdeprecated-dynamic-exception-spec), and from C++17 [`-Wdynamic-exception-spec`](https://clang.llvm.org/docs/DiagnosticsReference.html#wdynamic-exception-spec) for every form except `throw()` | The note that `std::unexpected_handler` shall not be used |
| M15-0-3 | Control shall not be transferred into a try or catch block using a goto or a switch statement. | ill-formed by [[except.pre]/3](https://eel.is/c++draft/except.pre#3) | |
| M15-1-1 | The assignment-expression of a throw statement shall not itself cause an exception to be thrown. | [`bugprone-exception-copy-constructor-throws`](https://clang.llvm.org/extra/clang-tidy/checks/bugprone/exception-copy-constructor-throws.html) | Every other potentially-throwing expression in the operand |
| M15-1-2 | NULL shall not be thrown explicitly. | [`bugprone-std-exception-baseclass`](https://clang.llvm.org/extra/clang-tidy/checks/bugprone/std-exception-baseclass.html) | |
| M15-1-3 | An empty throw (throw;) shall only be used in the compound statement of a catch handler. | | |
| M15-3-3 | Handlers of a function-try-block implementation of a class constructor or destructor shall not reference non-static members from this class or its bases. | [`-Wexceptions`](https://clang.llvm.org/docs/DiagnosticsReference.html#wexceptions) | The documented diagnostic names non-static members, not bases |
| M15-3-6 | Where multiple handlers are provided in a single try-catch statement or function-try-block for a derived class and some or all of its bases, the handlers shall be ordered most-derived to base class. | [`-Wexceptions`](https://clang.llvm.org/docs/DiagnosticsReference.html#wexceptions) | |
| M15-3-7 | Where multiple handlers are provided in a single try-catch statement or function-try-block, any ellipsis (catch-all) handler shall occur last. | ill-formed by [[except.handle]/5](https://eel.is/c++draft/except.handle#5) | |

### Other

| Rule | Requirement | Reason |
|---|---|---|
| A15-0-1 | A function shall not exit with an exception if it is able to complete its task. | Cannot determine intent from syntax |
| A15-0-2 | At least the basic guarantee for exception safety shall be provided for all operations. In addition, each function may offer either the strong guarantee or the nothrow guarantee. | Side effects on the unwinding path can be detected, but judging them requires telling damage from repair, and the repair is itself a side effect |
| A15-0-4 | Unchecked exceptions shall be used to represent errors from which the caller cannot reasonably be expected to recover. | Complete list of exceptions can be provided. Intent is out of scope |
| A15-0-5 | Checked exceptions shall be used to represent errors from which the caller can reasonably be expected to recover. | Complete list of exceptions can be provided. Intent is out of scope |

### Execution time and toolchain

These rules are about the exception handling runtime and its timing. The
exception graph says which paths exist, not how long they take or whether the
unwinder is correct.

| Rule | Requirement |
|---|---|
| A15-0-6 | An analysis shall be performed to analyze the failure modes of exception handling. In particular, the following failure modes shall be analyzed: (a) worst time execution time not existing or cannot be determined, (b) stack not correctly unwound, (c) exception not thrown, other exception thrown, wrong catch activated, (d) memory not available while exception handling. |
| A15-0-7 | Exception handling mechanism shall guarantee a deterministic worst-case time execution time. |
| A15-0-8 | A worst-case execution time (WCET) analysis shall be performed to determine maximum execution time constraints of the software, covering in particular the exceptions processing. |
