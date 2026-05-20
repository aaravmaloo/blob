<h1 align="center">blob: minimal note manager</h1>


`blob` is a minimalistic and efficient note manager that stays out of your way. It is programmed in C and is supposed to have the minimal binary size and the most minimalistic interface possible.

## Features
- blob features a easy to use UI inside the terminal to manage all your notes.
- deleting notes
- adding notes
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

compile with compression flags:

`gcc -Os -s -ffunction-sections -fdata-sections "-Wl,--gc-sections" --static main.c -o blob.exe`

## Keybindings

| Key | Action |
|-----|--------|
| `↑/↓` | Navigate notes |
| `Enter` | Open note |
| `n` | Create note |
| `d` | Delete note |
| `q` | Quit |

## Storage

blob stores notes in:

Linux:
```text
~/.local/share/blob/notes
```

Windows:
```text
%USERPROFILE%\AppData\Local\blob\notes
```

## Philosophy

blob is designed to be:
- minimal
- keyboard-driven
- fast
- dependency-free
- simple enough to understand in one source file
