# 0017. Vim as an input handler over the command layer

- Status: Accepted
- Date: 2026-10-07

## Context

[ADR 0005](0005-commands-and-selections.md) and [ADR 0010](0010-command-layer.md) made every edit a command on an always-present `SelectionSet` and key handling a swappable `InputHandler`, so that vim would be a second handler and not a second editing path. M10 builds it. The ROADMAP asks for a solid core, not full compatibility: modes, operators with motions and text objects, registers, dot-repeat, marks and the jump list, macros, search, visual block through multi-cursor, an ex subset, a scripted test harness and runtime switching.

## Decision

### Where it lives

- **All of vim is in `core`, headless** ([ADR 0003](0003-module-layout.md)): `VimInputHandler` (`src/core/vim/vimhandler.h`, state machine and parsing in `vimhandler.cpp`, operators/registers/marks in `vimoperators.cpp`, search and ex in `vimex.cpp`), pure motion functions over a rope (`vimmotions`), text objects (`vimtextobjects`), the key notation (`vimkeys`) and the pattern translation (`vimregex`). The handler is a `QObject` so a host status bar binds to `mode`, `pendingKeys`, `commandLine`, `recordingRegister` and `message`; `writeRequested`, `quitRequested` and `exCommand` hand `:w`, `:q` and unknown ex commands to the host. `CodeEditor` owns one and exposes it as `vim`; `vimMode` swaps it with the default handler (`setInputHandler` now calls `deactivate`/`activate`, so leaving vim ends an open insert as `<Esc>` would).
- **New `InputHandler` hooks, defaulted so the default keymap is unchanged:** `activate`/`deactivate`, `cursorShape` (line, block, underline), `cursorOffset` (the character a cursor is on; -1 draws none), `acceptsTextInput` (false in normal mode: input methods are disabled, so keys stay commands) and `commitText` (input-method text goes through the handler, so dot-repeat and macros see it). `InputHost` gained `visibleRows` (for `H M L` and scrolling), the system and selection clipboards (`"+` `"*`) and `setSearchHighlight`.

### Keys as symbols

A key is a symbol: a printable character is its own text, anything else is vim's notation (`<Esc>`, `<CR>`, `<C-d>`, `<Up>`). Macros, dot-repeat, ex `:normal` and the tests all work on symbol lists, and a macro register holds readable text (`"ap` pastes it, `"ayy` then `@a` runs a line). Keys with Ctrl or Alt that vim has no use for are not handled, so host shortcuts keep working; other printable keys are swallowed in normal mode.

### Cursor model

The `SelectionSet` stays between characters. In normal mode every selection is an empty cursor on a character (never past the last one of a line, except on an empty line); the handler clamps after each command. In visual modes the selection is the inclusive range and the handler keeps each selection's anchor and cursor characters; the `SelectionSet` is derived from them (characterwise: up to the end of the cursor's character; linewise: whole lines with the line break; block: one selection per line over a display-column range, tabs expanded). If the selections no longer match what the handler wrote (a mouse drag, a host call), it follows them: a non-empty selection is visual mode.

### Grammar

Normal mode accumulates keys and re-parses them after each one: `[count]["reg][count]{operator}[count]{motion | text object | line}`, motions and commands with one or two keys, and `:`, `/` and `?` which run to `<CR>`. Incomplete is a state (`OperatorPending`, `CommandLine`), not a mode flag, so the pending keys can be shown and edited (`<BS>`, `<C-u>`, `<C-w>`, history with `<Up>`/`<Down>`). Searching is an ordinary motion (`d/foo<CR>`, `v/foo<CR>` work and repeat with `.`).

Motions return where they land and whether they are exclusive, inclusive or linewise; the operator turns that into a range with vim's rules (an exclusive motion ending in column 0 stops at the end of the previous line, and becomes linewise when it also started at or before the first non-blank; `cw` is `ce`; `dw` on the last word of a line stops at the line's end). Motions use the display map where vim does (`gj`/`gk`, `H M L`) and the fold map for `j`/`k`.

### Edits, undo and multi-cursor (VIM-12)

- **Every operator applies to every selection**, each from its own cursor, through `commands::applyReplacements` as one transaction. Overlapping ranges merge; cursors that end up touching merge in the `SelectionSet`. Insert mode types at every cursor with the default commands (so auto-pairing and smart tabs apply), `p` distributes one register piece per cursor when the counts match (yanks with several cursors store one piece per cursor, joined by lines as the clipboard does), and `.` and macros replay on the whole set. `<Esc>` in normal mode with several cursors keeps the primary only, like the default handler; after a block insert the cursors collapse to the block's top-left.
- **One undo step per change.** The handler holds an edit group open from the first edit of a command until it finishes; an insert session (`i a o c s ...` until `<Esc>`) is one group, so `u` undoes `cwfoo<Esc>` whole. Macros, `.`, `:g` and `:normal` hold the group over everything they replay, as vim's undo sync does. After undo, selections collapse to the start of their range (normal mode).
- **Dot-repeat records the keys** of the last change (without its count and register) from its first key to leaving insert mode, and replays them through the handler; a new count replaces the old one. A visual change repeats over a region of the same size at the cursor. Macros record every key typed (not those a macro replays) as text in the register. A failed motion ends a replay.
- **Registers:** unnamed, `a–z` (append with `A–Z`), `0`, `1–9` (shifted by linewise and multi-line deletes and by `%`, search and paragraph motions), `-`, `_`, `+`/`*` through the host clipboards, read-only `.`, `:`, `/`. Each carries its type (characterwise, linewise, block) and per-cursor pieces.
- **Marks are anchors** (`ma`, `'a`, `` `a ``, and the automatic `'` `.` `^` `<` `>` `[` `]`), so they follow edits; the jump list is a list of anchors (100, one per line) pushed by jump motions and walked with `<C-o>`/`<C-i>`.
- **Autoindent is on** (new lines from `o`, `O`, `cc`, `S` and `<CR>` keep the indentation), as in Neovim; `ctx.settings.insertSpaces`/`indentWidth`/`tabWidth` decide what `>`, `<` and `<Tab>` insert.

### Search and ex

- **One regex engine for search, `:s`, `:g` and later find/replace (API-04):** `compilePattern` translates vim's syntax (magic by default, `\v \m \M \V`, `\< \>`, `\{n,m}`, `\zs`, classes, collections, `\c \C`, smartcase) to `QRegularExpression`; `search::findRegex` and `findAllRegex` (`core/textsearch`) apply it line by line, so the rope is never turned into one string and matches do not span lines (patterns that need to are rejected). Plain text patterns (and `\<word\>`, which is what `*` makes) use the rope's chunked substring search, which is 25 times faster on a miss.
- **Highlight is an editor feature:** the handler sets a pattern on the host; `CodeEditor` marks the matches in the rows of the frame plan (each row on its own text), with the theme color `searchMatch`. `:noh` and `:set nohls` clear it.
- **Ex subset:** ranges (`N . $ % 'x /pat/ ?pat? +N -N`, `,` and `;`, `'<,'>`), `:s` (flags `g i I n`, `& \0-\9 \r \t \u \U \l \L \e`), `:g`/`:g!`/`:v` (matching lines are anchored first; a line an earlier command made unmatching is skipped), `:d`, `:y`, `:normal`, `:>`, `:<`, `:N`, `:k`/`:mark`, `:noh`, `:set` (`ic scs hls`), `:w :q :wq :x` as signals; anything else is emitted as `exCommand`.

### Bounds

Scans stay within the keystroke budget: bracket matching 100k units (existing), sentences 20k units and paragraphs 10k lines (the motion does not move beyond that), word and character motions scan only as far as they move. A regex search that never matches walks every line (0.9 s on 2M lines; a plain-text miss takes 33 ms); moving find to a worker thread is API-04.

## Consequences

- Benchmarks (`bench_vim`, 2M lines / 90 MB, release): typical commands cost 2–30 µs (`w`, `j`, `dd`, `ciw`, `.`, `u`, plain `/` and `*`); `dap` 81 µs; a 1000-row block insert 5 ms; `:1,100000s/line/LINE/` 245 ms; 1000 cursors `x` 3.5 ms. Scrolling with the block cursor costs nothing measurable; with a search pattern matching in every row (`e`), sync and render grow by about 0.3 and 2.4 ms per frame (offscreen software backend). Keystroke-to-frame in vim mode was not measured separately: the handler adds microseconds to the existing path.
- Not done, by design: `.` of a command with a different count inside the operator, `:s` confirm, look-around and multi-line patterns, `g;`/changelists, named/quote registers beyond the list above, text-object counts for quotes, `=`, `!`, `gq`, folding commands beyond open/close/all, windows and tabs.
- Vim is on by default nowhere; the demo has Edit ▸ Vim mode and `--vim`/`--vim-keys` for screenshots.
