#!/usr/bin/env bash
# Probe which decomp TUs compile 64-bit standalone with the decomp include tree.
# Prints the passing set (as build_manifest.txt entries) and a summary to stderr.
# Use it to refresh tools/mirror/build_manifest.txt as headers evolve.
set -u
cd "$(dirname "$0")/../.." || exit 1
INC=(-Idecomp/include -Idecomp -Idecomp/src)
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
ok=0; fail=0
while IFS= read -r f; do
  rel="${f#decomp/}"
  if clang -m64 -c -w "${INC[@]}" "$f" -o "$tmp/probe.o" 2>/dev/null; then
    echo "$rel"; ok=$((ok+1))
  else
    fail=$((fail+1))
  fi
done < <(find decomp/src/main -name '*.c' | sort)
echo "probe: $ok compile, $fail fail" >&2
