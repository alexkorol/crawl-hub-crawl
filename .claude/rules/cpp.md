---
paths:
  - "crawl-ref/source/**/*.cc"
  - "crawl-ref/source/**/*.h"
---
- Follow `crawl-ref/docs/develop/coding_conventions.md`: 4-space indent, braces on their own line, 80 columns, no tabs.
- Before using any `you.`, `env.`, `crawl_state.`, `dgn_`, `mons_` symbol, confirm it exists in this checkout (clangd or `grep -rn`).
- New hub code goes in `crawl-ref/source/hub/*.cc|*.h`; wrap call-outs in upstream files with `#ifdef HUB`.
- Touching `tags.cc`, `tag-version.h`, `item-def.h`, `artefact.h`: read `crawl-ref/docs/develop/save_compatibility.txt` first and bump `TAG_MINOR_VERSION` when adding fields.
- After editing, run `util/checkwhite -n <file>` and `util/unbrace -n <file>` (the PostToolUse hook does this too).
