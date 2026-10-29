---
name: save-compat-bump
description: Checklist for adding a persisted field, item prop or marshalled struct to crawl without corrupting saves. Use when editing tags.cc, tag-version.h, item-def.h, artefact props or anything marshalled.
---
# Save compatibility bump

1. Read `crawl-ref/docs/develop/save_compatibility.txt` in full.
2. Add a new enumerator at the end of `tag_minor_version` in `crawl-ref/source/tag-version.h`,
   before `NUM_TAG_MINORS`, with a comment saying what it gates.
3. In `tags.cc`, write the new field unconditionally; read it guarded by
   `if (th.getMinorVersion() >= TAG_MINOR_<NAME>)` with a sensible default on the else path.
4. Never reorder or remove existing marshalled fields on the 0.34 branch.
5. Rebuild, then load a pre-change save (keep one under `saves/` from before the edit) and a new one.
6. Run `./crawl -test` and `make catch2-tests`.
