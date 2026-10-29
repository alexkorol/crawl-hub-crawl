#!/bin/bash
# PostToolUse hook: run crawl's whitespace/brace linters on an edited C++ file (report only).
input=$(cat)
file=$(printf '%s' "$input" | /usr/bin/python3 -c 'import json,sys; print(json.load(sys.stdin).get("tool_input",{}).get("file_path",""))' 2>/dev/null)
case "$file" in
  *.cc|*.h)
    root="$(cd "$(dirname "$0")/../.." && pwd)"
    cd "$root/crawl-ref/source" || exit 0
    rel="${file#$root/crawl-ref/source/}"
    out=$("$root/crawl-ref/source/util/checkwhite" -n "$rel" 2>&1; "$root/crawl-ref/source/util/unbrace" -n "$rel" 2>&1)
    if [ -n "$out" ]; then echo "lint ($rel):"; echo "$out"; fi
    ;;
esac
exit 0
