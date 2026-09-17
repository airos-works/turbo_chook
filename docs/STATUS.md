# Verification record

## Fixes after the first round of testing (2026-09-17)

Nine issues from the first test session were fixed, one commit each, and
checked on the beta6 VM with the demo repository plus a clone of
`mity/md4c` (841 commits, real merges, GitHub pull requests):

| Issue | Fix | Checked |
| --- | --- | --- |
| #1 icon | Redrawn bird with HVIF level-of-detail shapes | Native renders at 16, 32, 64, 128 and 256 px |
| #2 themes | Look & Feel tab: font family, size, ten themes plus System | Obsidian at 14 pt applied live, System restored |
| #3 scrolling | History reloads only when refs move and keeps its position | Paged md4c history; no jumps on refresh or selection |
| #4 remotes | Section and remote collapse state survive rebuilds; big remotes start collapsed | Rebuilds after commits keep `origin` closed |
| #5 sidebar | Labels no longer bound the layout's maximum width; explicit minimums; width saved | Splitter dragged both ways, 260 px default |
| #6 diff scrolling | Content drawn in scrolled coordinates, pinned gutter, no reload of an unchanged diff | Long line scrolled right and back cleanly |
| #7 Markdown | md4c-based renderer for pull request text | md4c PR #425 with headings, code, links and a table |
| #8 pull requests | No reload on the echoed sidebar selection; one reload per minute unless asked | Card opened once; list stable |
| #9 graph | Stable lane columns with reuse, continuous per-row edges, horizontal links with rounded corners offset above/below the node, distinct lane colours, graph column sized to the 95th percentile with a fade for wider rows, `--date-order` | 7,005-commit platform-ui history: zero discontinuities; dense and wide regions checked on screen; random-DAG continuity tests |

## 0.1.0 alpha (2026-09-17)

Everything below was exercised on the Turbo Chook VM (Haiku R1~beta6
hrev59866+79, x86_64) described in [VM.md](VM.md), against the demo repository
from `tools/create-demo.sh`, with screenshots taken through QMP.

## Build and tests

| Check | Result |
| --- | --- |
| `make -j6` natively with gcc 13.3.0 | builds `build-haiku/TurboChook`, no errors |
| `make check-host` on Haiku | 185 checks, 0 failures |
| `make BUILD=build-host check-host` on Linux (g++ 13.3, git 2.43) | 185 checks, 0 failures |
| `make package` | `artifacts/turbochook-0.1.0~alpha-1-x86_64.hpkg` (816 KB); icon attribute matches the HVIF |
| `pkgman install` of that package | `/boot/system/apps/TurboChook` with signature `application/x-vnd.TurboChook` and a Deskbar Applications entry; the installed binary opens the demo repository |

The core tests cover status/log/branch/tag/stash/remote/submodule/name-status
parsing, GitHub JSON parsing and error messages, graph lane layout with merges
and convergences, unified diff parsing, whole-hunk and selected-line patch
generation, settings persistence, Git discovery and version ordering, and a
live repository: init, commit, hunk staging through `git apply`, unstaging,
discarding a hunk, backup and restore, branching, merging, tags, stashes,
revert, reset, cherry-pick, branch deletion, reflog, a conflicting merge with
state detection, resolution and abort, and the undo stack.

## Interactive checks

- Welcome window lists the recent repository; double-click opens it. A folder
  dropped from Tracker opens as a repository.
- Sidebar: Repository, Branches (current bold, `↑1 ↓1` badges), Remotes with
  their branches, Tags, Stashes; sections collapse; counts on section headers.
- Working copy: unstaged (2) and staged (1) lists with status badges; selecting
  `src/main.cpp` shows the diff with line numbers, colours and *Stage Hunk* /
  *Discard Hunk*. *Stage Hunk* moved the file to the staged list and enabled
  Undo ("Undo stage hunk in src/main.cpp"); Undo moved it back.
- Commit: typing a message and pressing Commit created `305d2b2 Document
  testing and add notes`; the history refreshed with `main` ahead of
  `origin/main` and the status bar showed `↑1 ↓1`.
- History: the graph shows the merges of `feature/parser` and `bugfix/crash`,
  the unmerged `feature/network`, tag badges `v0.1`/`v0.2`, remote badges and
  relative dates; stash refs are excluded. Selecting a commit shows its
  metadata, files (`M src/helpers.cpp +1 -0`) and the per-file diff. The
  context menu offers checkout, branch/tag here, cherry-pick, revert, three
  reset modes, compare and copy hash.
- Branch context menu: checkout, merge into main, rebase main onto, compare,
  push, force push, set upstream, new branch/tag here, rename, delete.
- Drag & drop: `feature/network` dropped on `main` offered *Merge
  feature/network into main*, *Rebase main onto feature/network* and *Compare*;
  the stash dropped on *Working Copy* offered *Apply* and *Pop*.
- Preferences: the Git menu listed `Git 2.54.0 (system) /boot/system/bin/git`
  and `Git 2.45.2 (non-packaged) /boot/home/config/non-packaged/bin/git`;
  choosing 2.45.2 and Apply persisted `gitExecutable` in
  `settings.json` and the window reported `Active: Git 2.45.2`.

## Not yet verified

- Pull requests against a live GitHub repository (the client, JSON parsing and
  error handling are unit tested; the VM has no token). Fetch, pull and push
  were exercised only against the local bare remote through the core tests.
- Conflict resolution buttons in the UI (the underlying operations are tested).
- Line-level staging in the UI (the patch builder is tested).
- An ARM64 build: the code is architecture neutral but has only been compiled
  for x86_64.

## Known limitations

- Rebase is non-interactive; reordering or squashing commits by dragging inside
  the history is not implemented.
- Undo entries live for the lifetime of the window and rely on `reset --hard`
  plus an automatic backup for merge/rebase/pull; they are not persisted.
- Refresh polls every 4 seconds while the window is active rather than watching
  the working tree.
