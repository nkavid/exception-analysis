#!/usr/bin/env bash
set -euo pipefail

label=$1 out=$2 covered_inputs=$3
shift 3

directories=() ignore_directories=() names=() libraries=() fail_on_uncovered=
for arg in "$@"; do
  case ${arg} in
    -L*) directories+=("${arg#-L}") ;;
    -l*) names+=("${arg#-l}") ;;
    --ignore=*) ignore_directories+=("${arg#--ignore=}") ;;
    --fail-on-uncovered=true | --fail-on-uncovered=false) fail_on_uncovered=${arg#--fail-on-uncovered=} ;;
    --fail-on-uncovered=*)
      echo "${label}: ${arg}: expected --fail-on-uncovered=true or --fail-on-uncovered=false" >&2
      exit 1
      ;;
    *) libraries+=("${arg}") ;;
  esac
done

if [[ -z ${fail_on_uncovered} ]]; then
  echo "${label}: missing --fail-on-uncovered=true or --fail-on-uncovered=false" >&2
  exit 1
fi

resolve() {
  for directory in "${directories[@]}"; do
    for library in "${directory}/lib$1.so" "${directory}/lib$1.a"; do
      if [[ -e ${library} ]]; then
        echo "${library}"
        return
      fi
    done
  done
  echo "${label}: -l$1: neither lib$1.so nor lib$1.a in ${directories[*]}" >&2
  return 1
}

for name in "${names[@]}"; do
  library=$(resolve "${name}")
  libraries+=("${library}")
done

covered=() ignored_libraries=() ignored_directories=() uncovered=()
for library in "${libraries[@]}"; do
  if grep -Fxq "${library}" "${covered_inputs}"; then
    covered+=("${library}")
    continue
  fi
  for directory in "${ignore_directories[@]}"; do
    if [[ ${library} == "${directory}"/* ]]; then
      ignored_libraries+=("${library}")
      ignored_directories+=("${directory}")
      continue 2
    fi
  done
  uncovered+=("${library}")
done

json_array() {
  jq --null-input '$ARGS.positional' --args "$@"
}

jq --null-input \
  --argjson covered "$(json_array "${covered[@]}")" \
  --argjson ignored_libraries "$(json_array "${ignored_libraries[@]}")" \
  --argjson ignored_directories "$(json_array "${ignored_directories[@]}")" \
  --argjson uncovered "$(json_array "${uncovered[@]}")" \
  '{
    covered: $covered,
    ignored: [[$ignored_libraries, $ignored_directories] | transpose[] | {library: .[0], directory: .[1]}],
    uncovered: $uncovered
  }' > "${out}"

if [[ ${fail_on_uncovered} == true ]] && (( ${#uncovered[@]} )); then
  echo "${label}: ${#uncovered[@]} linked libraries are not produced by deps, implementation_deps, dynamic_deps, cc_toolchain, or known_ignored:" >&2
  printf '%s\n' "${uncovered[@]}" >&2
  exit 1
fi
