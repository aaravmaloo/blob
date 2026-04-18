# BLOB

**B**rain **L**edger **O**f **B**riefs — a terminal-first note-taking app built with [Bubble Tea](https://github.com/charmbracelet/bubbletea).

BLOB is a keyboard-driven, modal note editor that lives in your terminal. It stores plain Markdown files on disk, renders previews with Glamour, and stays out of your way. Think of it as a minimal Obsidian for people who live in the shell.

---

## Features

### Core

- **File-based storage** — Notes are plain `.md` files under `~/.blob`. No database, no lock-in. Edit them with any tool.
- **Split / Zen / Preview layouts** — Work in a split view, go fullscreen with Zen mode, or render Markdown inline with the preview pane.
- **Vim modal editing** — Full vim keybindings (h/j/k/l, i/a/o, x, 0/$) with a toggle for non-vimmers. Insert mode and Normal mode are clearly indicated.
- **Fuzzy search** — Press `/` to fuzzy-find across all notes instantly.
- **Lock & password protection** — Lock individual notes or the entire app behind a password (SHA-256 hashed, stored locally).
- **Custom themes** — Drop a `theme.css` in `~/.blob/` to override colors. Ships with a purple-accent dark theme.

### Scratch Pad

A persistent quick-capture buffer. Press `s` to drop into it, type a thought, press `s` again to save and return. No need to create a new note for every random idea. The scratch pad persists across sessions — it's always there when you need it.

### Recent Notes Sidebar

The browser panel shows the last 5 recently accessed notes in a dedicated section at the top. Jump between recent work without navigating the folder tree. Muscle memory for productivity nerds.

### Word Count & Stats Bar

When editing, the status bar and editor footer show:
- **Word count** — split by whitespace, standard writer's count
- **Character count** — rune-accurate for Unicode
- **Line count** — total lines in the file
- **Reading time** — estimated at 200 WPM, formatted as `Xm` or `Xh Xm`

Writers and students eat this up.

### Quick Note Linking

Press `Ctrl+K` to open a fuzzy finder, search for a note, and insert a `[[Note Name]]` style link at the cursor position. Makes cross-referencing painless — a VSCode-style command palette for note linking.

### Pinned Notes

Press `.` in the browser panel to pin (or unpin) a note. Pinned notes float to the top of the browser regardless of folder structure, marked with a ★ icon. Keep important stuff accessible.

### Archive Mode

Press `a` in the browser to mark a note as archived (instead of deleting it). Archived notes are hidden by default. Press `A` to toggle visibility. People hate losing old notes but also hate clutter — archive gives you the best of both worlds.

### Note Templates with Variables

- Press `T` to save the current note as a reusable template.
- Press `t` to create a new note from a template (fuzzy-filtered picker).

Templates support auto-expanding placeholders:

| Variable       | Expands to          |
|----------------|---------------------|
| `{{date}}`     | `2006-01-02`        |
| `{{time}}`     | `15:04`             |
| `{{datetime}}` | `2006-01-02 15:04`  |
| `{{title}}`    | Note filename       |
| `{{year}}`     | `2006`              |
| `{{month}}`    | `January`           |
| `{{day}}`      | `02`                |
| `{{weekday}}`  | `Monday`            |

Perfect for meeting notes, daily logs, standup templates, etc.

---

## Installation

### From Source

```bash
git clone https://github.com/aaravmaloo/blob.git
cd blob
go build -o blob .
./blob
```

### Requirements

- Go 1.23+
- A terminal with true color support (recommended)

---

## Keybindings

### Global (outside insert mode)

| Key              | Action                              |
|------------------|-------------------------------------|
| `n`              | New note (browser panel)            |
| `N`              | New folder (browser panel)          |
| `t`              | New note from template              |
| `T`              | Save current note as template       |
| `r`              | Rename selected note/folder         |
| `d`              | Delete selected note/folder         |
| `p` / `Tab`     | Switch panel focus                  |
| `z`              | Toggle zen (fullscreen editor)      |
| `/`              | Fuzzy search notes                  |
| `m`              | Toggle Markdown preview             |
| `v`              | Toggle vim mode on/off              |
| `s`              | Open scratch pad (s again to save)  |
| `.`              | Pin/unpin note (browser panel)      |
| `a`              | Archive/unarchive note (browser)    |
| `A`              | Toggle archived note visibility     |
| `Ctrl+K`         | Insert `[[link]]` to another note   |
| `L`              | Lock/unlock current note            |
| `Ctrl+L`         | Lock/unlock the app                 |
| `H` / `Ctrl+H`  | Toggle help panel                   |
| `q`              | Quit (normal mode only)             |

### Browser Panel

| Key                | Action                |
|--------------------|-----------------------|
| `↑` / `k`         | Move up               |
| `↓` / `j`         | Move down             |
| `Enter`            | Expand folder / edit note |
| `Shift+Enter`      | Open note in zen mode |

### Editor (Vim Normal Mode)

| Key  | Action            |
|------|-------------------|
| `h`  | Move left         |
| `l`  | Move right        |
| `k`  | Move up           |
| `j`  | Move down         |
| `0`  | Start of line     |
| `$`  | End of line       |
| `i`  | Enter insert mode |
| `a`  | Append (insert after cursor) |
| `o`  | Open line below   |
| `x`  | Delete character  |

### Editor (Insert Mode)

Standard typing, arrow keys, backspace, delete, tab. Press `Esc` to return to normal mode (when vim is enabled).

### Fallback Shortcuts

For terminals that swallow key events:

| Key              | Equivalent  |
|------------------|-------------|
| `Ctrl+N`         | `n`         |
| `Ctrl+Shift+N`   | `N`         |
| `Ctrl+Shift+P`   | `p`         |
| `Ctrl+Shift+F`   | `z`         |
| `Ctrl+Shift+V`   | `v`         |

---

## Storage Layout

```
~/.blob/
├── welcome-to-blob.md      # Your notes
├── ideas/
│   └── project-idea.md     # Organized in folders
├── .meta.json               # Pins, archives, recent, templates, scratch
├── .locks.json              # Password hashes for locked items
└── theme.css                # Optional custom theme
```

All notes are plain Markdown. You can edit them outside BLOB with any editor — BLOB will pick up changes on next reload.

---

## Theming

Create `~/.blob/theme.css` to customize colors:

```css
:root {
  --surface: #000000;
  --panel-surface: #000000;
  --panel-edge: #7c3aed;
  --panel-glow: #a78bfa;
  --ink: #e2e8f0;
  --muted: #64748b;
  --accent: #a78bfa;
  --cyan: #22d3ee;
  --rose: #fb7185;
  --selected-bg: #7c3aed;
  --selected-fg: #ffffff;
  --gold: #fbbf24;
  --green: #4ade80;
}
```

---

## Architecture

| File           | Purpose                                      |
|----------------|----------------------------------------------|
| `main.go`      | Entry point, initializes storage and TUI     |
| `ui.go`        | Bubble Tea model, key handling, rendering    |
| `editor.go`    | Modal text editor state machine              |
| `storage.go`   | File system operations, tree loading         |
| `meta.go`      | Persistent metadata (pins, archives, recent, templates, scratch) |
| `lock.go`      | Password-protected locking for notes & app   |
| `theme.go`     | CSS variable parser for custom themes        |
| `theme.css`    | Default theme color definitions              |

---

## License

MIT
