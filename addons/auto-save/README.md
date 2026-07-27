# auto-save

[api] = 2
[version] = 1.0.0
[authors] = {"blob"}
[description] = <Creates a timestamped backup of the selected note in a .backups folder>
[keybind] = b
[mode] = note
[permissions] = {"read-note","write-notes"}

Creates a timestamped copy of the selected note inside a `.backups/` directory next to your notes. Each backup is named like `my-note_20260327_143000.md` so you can easily recover previous versions.

Run it periodically (or bind it to a key) to maintain a version history of your notes without needing git.

Usage: `auto-save <note_path>`
