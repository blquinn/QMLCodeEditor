# 0019. Find and replace: one search layer, worker threads on snapshots, an opt-in find bar

- Status: Accepted
- Date: 2026-10-07

## Context

API-04 asks for incremental search with regex, match highlighting and a replace-all that is one transaction, running on rope snapshots in a worker thread. Vim mode (M10) had already built most of the engine: a pattern compiled to a `QRegularExpression` plus, for plain text, the rope's chunked substring search ([ADR 0017](0017-vim-input-handler.md)). But that code lived in the vim handler: the literal-versus-regex choice was inline in `evalSearch`, `:s` had its own loop over lines, and the compiled pattern type was `vim::CompiledPattern`. A second search feature would have copied all of it.

## Decision

### One search layer (`core/textsearch`)

- **`search::Pattern`** is the compiled pattern both features use: the regex, and the literal text when the pattern is plain (with its case and whole-word flags). `vim::CompiledPattern` is now an alias for it. Vim's `compilePattern` translates vim syntax to one; `search::compileQuery` builds one from find-box text (plain text or a `QRegularExpression`, case, whole word).
- **`search::find` / `search::findAll`** pick the engine from the pattern (chunked literal search or the line-by-line regex), and `findAll` takes a `std::atomic_bool` cancel flag, polled per chunk or per line. Vim's `/`, `n`, `*` and ex address searches call `find`; find/replace calls both.
- **`search::forEachLineMatch`** is the per-line `globalMatch` loop. `:s`, replace-all in regex mode and `findAllRegex` all sit on it.
- **Dialects stay separate.** Vim keeps its pattern syntax and `\1`/`&` replacements (`vim::expandReplacement`); find uses QRegularExpression syntax and `$1`, `$&`, `$$`, `\n`, `\t` (`search::expandReplacement`). Plain-text mode inserts the replacement as typed. The shared part is the engine, not the user-visible syntax.
- Both are line-by-line: a regex never spans lines. A plain-text needle that contains a line break is found (the chunked search is not line based) but is not drawn, since the highlight is matched per visible row.

### `FindReplace` (`core/findreplace`), exposed as `editor.find`

- **A core QObject, like `VimInputHandler`**, constructed with the document, the selection set and an `EditContext` provider, so it is tested without a window. The editor owns one and wires three things: the highlight pattern, "selection moved" and "reveal".
- **Counting runs on a worker thread over `TextDocument::snapshot()`.** Each search carries a generation and a cancel flag. A new query, or an edit (after 30 ms of quiet), cancels the running search and starts another; a result from an older generation is dropped. The list holds at most 100,000 matches and `capped` says there are more. The result for a text that has since changed is shown but flagged stale, and a new search is queued.
- **Navigation is a binary search on the finished list.** When the list is stale, still being built, or capped past the cursor, `next()`/`previous()` search the live rope directly with `search::find`, which stops at the first hit. That path can scan a whole document on the GUI thread for a pattern with no match (about 35 ms of literal search per 90 MB, about 1 s of regex); it only happens when the user navigates before the first search has finished.
- **Incremental search remembers an origin**: the cursor where the query started being typed, moved only by next/previous/replace and by selection changes the user made. Each new query selects the first match at or after the origin, so typing `foo` letter by letter doesn't walk forward.
- **Replace-all builds its edits on a snapshot in a worker**, then applies them on the GUI thread in one edit group (one undo step) only if the document is still at the snapshot's version; otherwise it builds them again. It does not use `commands::applyReplacements`, which leaves a selection behind every replacement (two anchors each): a replace-all ends with a single cursor after the last replacement. Empty regex matches are replaced (`^` → `// `), unlike in the count.
- **Highlight is the pattern, not the match list.** The editor marks matches in the rows of the frame plan with the same per-row loop as vim's `hlsearch` (bounded to 4000 per pattern), so highlighting costs nothing when the list is large or stale. The two patterns can be on at once. The current match is the selection; there is no separate "current match" color.

### The find bar is opt-in (`FindBar.qml`)

The editor draws no find UI and binds no Ctrl+F. `FindBar.qml` is a plain QtQuick item (no Controls dependency) built only on `editor.find`; a host instantiates it and binds its own shortcuts to `open(withReplace)`, or builds a different bar on the same object. It follows the popup delegate convention from [ADR 0006](0006-anchored-decorations.md) in taking the editor as a required property and its colors from `editor.theme`.

## Consequences

- Vim and find/replace now agree on what a match is, and an improvement to the engine (a faster regex path, multi-line patterns) reaches both.
- Regex search is the slow path: about 1 s for a 2M-line document, on the worker. If it matters, extracting a required literal from the expression to pre-filter lines is the next step; `bench_find` tracks it (`worker/regex_*`).
- Searching and replacing are blocked while the document is loading (`replace*` refuse; counting restarts as slices arrive).
- Not covered: multi-line patterns, find in selection, find across several documents. The first two are natural extensions of `search::Pattern` and `FindReplace`; none needs a change of structure.
