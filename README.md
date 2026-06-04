<h1 align="center">blob: minimal note manager</h1>


`blob` is a minimalistic and efficient inline terminal note manager that stays out of your way. It is programmed in C and keeps the interface small, fast, and terminal-native.

## Features
- inline interactive note selector
- markdown note creation
- live note search
- note deletion
- customizability

## Customizing blob
In the future, blob will support themes. For now, you can configure the editor that blob will use to edit the notes. This can be set via the `EDITOR` environment variable, and will force blob to use that editor. 

## Installation
blob can be installed via [github releases](https://github.com/aaravmaloo/blob/releases), or can be compiled from scratch. 
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
| `d` | Delete note |
| `/` | Search notes |
| `Esc` | Clear search |
| `q` | Quit |

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
- simple enough to understand in one source file
- an inline selector, not a fullscreen TUI
