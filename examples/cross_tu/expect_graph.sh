#!/usr/bin/env bash
set -euo pipefail

graph=""
for output in "$@"; do
  if [[ "${output}" == *.exa_graph.json ]]; then
    graph="${output}"
  fi
done
status=0

definitions='
def name_of($node): $g.nodes[$node].name // "";
def function_of_catch($catch): first($g.edges[] | select(.kind == "catch" and .to == $catch) | .from);
def unwinds_kind($kind): $e.unwinds[].to | $g.edges[.] | select(.kind == $kind);
def caught_by: unwinds_kind("catch") | .to;
def uncaught($kind; $reason): unwinds_kind("uncaught") | select($g.nodes[.to] | .kind == $kind and .reason == $reason);
'

expect() {
  local description="$1"
  local filter="$2"
  if ! jq -e ". as \$g | any(\$g.exceptions[]; . as \$e | \$g.nodes[\$e.source] as \$s | ${definitions} ${filter})" "${graph}" >/dev/null; then
    echo "missing: ${description}"
    status=1
  fi
}

expect "ParseError thrown in parsePort terminates in noexcept parsePortOrDefault" \
  "\$s.thrown_type == \"demo::errors::ParseError\"
   and any(uncaught(\"terminate\"; \"noexcept\"); name_of(.from) == \"demo::parser::parsePortOrDefault\")"

expect "ParseError thrown in parsePort is caught in serve" \
  "\$s.thrown_type == \"demo::errors::ParseError\"
   and any(caught_by;\$g.nodes[.].caught_type == \"demo::errors::ParseError\"
                       and (name_of(function_of_catch(.)) | endswith(\"serve\")))"

expect "ConfigError thrown in load escapes main" \
  "\$s.thrown_type == \"demo::errors::ConfigError\"
   and any(uncaught(\"terminate\"; \"no_handler\"); name_of(.from) == \"main\")"

expect "A rethrown in noexcept g terminates in g" \
  "\$s.thrown_type == \"demo::rethrow::A\"
   and any(uncaught(\"terminate\"; \"noexcept\"); name_of(.from) == \"demo::rethrow::g\")"

if ! jq -e '. as $g
            | any($g.edges[]; .kind == "uncaught"
                              and $g.nodes[.to].kind == "terminate"
                              and $g.nodes[.to].reason == "no_currently_handled_exception"
                              and $g.nodes[.from].name == "demo::rethrow::g")' "${graph}" >/dev/null; then
  echo "missing: throw; in g reached from main outside a handler terminates"
  status=1
fi

if ! jq -e '. as $g
            | [$g.edges[] | select(.kind == "call" and ($g.nodes[.to].name // "" | endswith("runNothrowHook"))) | .from] as $callees
            | any($g.edges[]; .kind == "throw" and (.to as $to | $callees | index($to)) and $g.nodes[.to].kind == "unknown_function")
            | not' "${graph}" >/dev/null; then
  echo "unexpected: unknown exception thrown by the unknown_function called in noexcept-typed runNothrowHook"
  status=1
fi

expect_edge() {
  local description="$1"
  local filter="$2"
  if ! jq -e ". as \$g | any(\$g.edges[]; . as \$edge | \$g.nodes[.from].name as \$from | \$g.nodes[.to].name as \$to | ${filter})" "${graph}" >/dev/null; then
    echo "missing: ${description}"
    status=1
  fi
}

expect_edge "call from parsePort into load crosses cc_target and translation_unit" \
  "\$edge.kind == \"call\" and \$from == \"demo::parser::parsePort\" and \$to == \"demo::config::load\"
   and \$edge.crossing.cc_target == {\"from\": \"@@//examples/cross_tu:parser\", \"to\": \"@@//examples/cross_tu:config\"}
   and \$edge.crossing.translation_unit == {\"from\": \"examples/cross_tu/parser.cpp\", \"to\": \"examples/cross_tu/config.cpp\"}"

expect_edge "call from noexcept g into h crosses exception_specification and nounwind only" \
  "\$edge.kind == \"call\" and \$from == \"demo::rethrow::g\" and \$to == \"demo::rethrow::h\"
   and \$edge.crossing == {\"exception_specification\": {\"from\": \"non_throwing\", \"to\": \"potentially_throwing\"},
                           \"nounwind\": {\"from\": true, \"to\": false}}"

if [[ ${status} -ne 0 ]]; then
  echo "graph: ${graph}"
fi
exit ${status}
