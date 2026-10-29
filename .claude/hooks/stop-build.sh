#!/bin/bash
# Stop hook: the debug webtiles build must pass before Claude stops. Exit 2 blocks the stop.
input=$(cat)
active=$(printf '%s' "$input" | /usr/bin/python3 -c 'import json,sys; print(json.load(sys.stdin).get("stop_hook_active", False))' 2>/dev/null)
[ "$active" = "True" ] && exit 0
root="$(cd "$(dirname "$0")/../.." && pwd)"
# Only build if a C++ source changed since HEAD or is untracked.
if ! git -C "$root" status --porcelain -- 'crawl-ref/source/*.cc' 'crawl-ref/source/*.h' 'crawl-ref/source/hub' | grep -q .; then exit 0; fi
export PATH="/opt/homebrew/opt/ccache/libexec:$root/.venv/bin:$PATH"
cd "$root/crawl-ref/source" || exit 0
if ! out=$(make -j8 debug WEBTILES=y 2>&1); then
  echo "Build failed; fix before stopping:" >&2
  echo "$out" | grep -E 'error|Error' | head -30 >&2
  exit 2
fi
exit 0
