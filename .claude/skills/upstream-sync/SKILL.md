---
name: upstream-sync
description: Rebase the fork onto the latest upstream stone_soup-0.34 (or move to the next stable branch). Manual-only procedure with side effects.
disable-model-invocation: true
---
# Upstream sync

1. `git fetch upstream` ; `git status` must be clean.
2. `git rebase upstream/stone_soup-0.34` (or `upstream/stone_soup-0.35` when moving stable lines).
3. Resolve conflicts with `merge.conflictStyle=diff3`; fork commits are small and flag-gated, so prefer
   upstream's version of shared code and re-apply the `#ifdef HUB` call-out.
4. Rebuild with `make -j8 debug WEBTILES=y`, run `./crawl -test`, `make catch2-tests`, `cd webserver && pytest`.
5. `git range-diff` against the previous fork tip to confirm only intended changes moved.
6. Push with `--force-with-lease` to `origin`.
