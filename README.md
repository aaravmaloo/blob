<h1 align="center">blob: minimal note manager</h1>

<p align="center">
  <img src="https://img.shields.io/github/actions/workflow/status/aaravmaloo/blob/ci.yml?label=build&style=flat-square" />
  <img src="https://img.shields.io/github/v/release/aaravmaloo/blob?style=flat-square" />
  <img src="https://img.shields.io/badge/license-GPL--2.0-green?style=flat-square" />
  <img src="https://img.shields.io/badge/language-C-blue?style=flat-square" />
</p>

<p align="center">
  <img src="assets/demo.gif" alt="Demo" width="700">
</p>


`blob` is a minimalistic and efficient inline terminal note manager that stays out of your way. It is programmed in C and keeps the interface small, fast, and terminal-native.

## Features
- inline interactive note selector
- markdown note creation
- live note search
- rename, trash, restore, and hard delete flows
- interactive trash bin viewer (`t`) — browse, restore, or purge trashed notes
- command palette for core actions and installed plugins
- relative timestamps and pagination
- safer plugin installs with API, mode, and permission metadata
- customizability

## Customizing blob
In the future, blob will support themes. For now, you can configure the editor that blob will use to edit the notes. This can be set via the `EDITOR` environment variable, and will force blob to use that editor. 

## Installation
blob can be installed via yay, winget, [github releases](https://github.com/aaravmaloo/blob/releases), or can be compiled from scratch. 

To install it using yay (arch linux), use the following command. 
`yay -S blob-bin`
and you will be good to go!

To install it using winget run the following command.
`winget install aaravmaloo.blob`
and you will be good to go!

To compile from scratch, use the following steps.
clone:

`git clone https://github.com/aaravmaloo/blob`

change dir:

`cd blob`

compile:

```sh
make
```

Or for an optimized release build:

```sh
make release
```

*Note: On Windows, you may need to use `mingw32-make` instead of `make` depending on your environment.*

## Keybindings

| Key | Action |
|-----|--------|
| `↑/↓` | Navigate notes |
| `Enter` | Open note |
| `n` | Create note |
| `r` | Rename note |
| `d` | Move note to trash |
| `D` | Permanently delete note |
| `t` | Open trash bin (restore or permanently delete trashed notes) |
| `y` | Copy note path |
| `/` | Search notes |
| `:` | Open command palette |
| `p` | Open plugins manager |
| `Esc` | Clear search |
| `q` | Quit |

## Plugins

blob supports optional, compiled plugins to extend its features (like encryption or sync) without bloating the core. Press `p` inside the interface to manage plugins. For every step/access blob does, it requires your permission for maximum safety.

Plugins declare an API version, run mode, and permissions in their README manifest. blob warns about legacy plugins, keybind conflicts, and plugins that require a newer API.

### Built-in plugins

| Plugin | Key | What it does |
|--------|-----|--------------|
| `lock` | — | Encrypt/decrypt notes with a password |
| `stats` | `s` | Show character, word, and line counts |
| `archive` | `a` | Move notes to an archive folder |
| `git-sync` | `g` | Commit and push notes to a git repo |
| `pin` | `i` | Pin/unpin notes (floated to top with prefix) |
| `fuzzy-search` | `f` | Fuzzy search across note titles and content |
| `tags` | `z` | Read, add, and clear tags metadata |
| `word-count` | `w` | Words, chars, lines, sentences, reading time |
| `open-dir` | `o` | Open the note's folder in your file explorer |
| `export` | `e` | Export to HTML, PDF, or DOCX via pandoc |
| `remind` | `m` | Set a timed system notification for a note |

If you are a developer and want to create your own plugin, see [PLUGIN_DEVELOPMENT.md](PLUGIN_DEVELOPMENT.md).

## Storage

blob stores notes in:

Linux:
```text
~/.local/share/blob/notes
```

macOS:
```text
~/Library/Application Support/blob/notes
```

Windows:
```text
%LOCALAPPDATA%\blob\notes
```

## Philosophy

blob is designed to be:
- minimal
- keyboard-driven
- fast
- dependency-free
- an inline selector, not a fullscreen TUI
