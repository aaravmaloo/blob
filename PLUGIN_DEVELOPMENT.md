# `blob` plugin development
`blob` supports a simple, compiled plugin architecture. Addons are standalone executables compiled on-device. They interact with `blob` via file path arguments and standard terminal IO, keeping `blob` lightweight, secure, and extensible.

---

## 1. Directory Structure

A plugin must reside in a dedicated folder under the `addons/` directory. `blob` scans for plugins in two locations:
1. System data directory (e.g. `%LOCALAPPDATA%\blob\addons` or `~/.local/share/blob/addons`)
2. Local workspace directory (`./addons/` relative to where `blob` is run)

The folder structure must look like this:
```text
addons/
└── my_plugin/              <-- Must match plugin name
    ├── my_plugin.c         <-- Source file
    └── README.md           <-- Documentation & manifest
```

On compilation, `blob` compiles `my_plugin.c` directly into a native executable in the same directory:
* **POSIX**: `addons/my_plugin/my_plugin`
* **Windows**: `addons/my_plugin\my_plugin.exe`

---

## 2. Plugin Manifest (`README.md`)

`blob` parses metadata directly from the plugin's `README.md` file. The top of the README must contain the following manifest block:

```markdown
# my_plugin

[authors] = {"Your Name", "Co-Author"}
[description] = <A brief description of what the plugin does>
[keybind] = x
```

* `# my_plugin`: Specifies the human-readable name of the plugin (must match the directory name).
* `[authors] = { ... }`: A list of authors.
* `[description] = < ... >`: A brief summary shown in `blob`'s plugin menu (`p`).
* `[keybind] = x`: **The magic shortcut.** In the notes list view, pressing the character key defined here (e.g., `x`) will immediately execute this plugin on the selected note.

---

## 3. Invocation Protocol

When a keypress triggers your plugin:
1. `blob` clears its terminal region and **disables raw terminal mode**.
2. `blob` spawns your plugin executable, passing the absolute path of the selected note as the first argument (`argv[1]`):
   ```bash
   addons/my_plugin/my_plugin "/path/to/notes/my-note.md"
   ```
3. Your plugin runs synchronously. Because raw mode is disabled, your plugin has **full control of stdout and stdin** (e.g., it can print prompts, mask passwords, or open interactive forms).
4. When your plugin exits with status `0`, `blob` re-enables raw terminal mode and refreshes the notes list.

---

## 4. Special Core Hooks (The `lock` Plugin)

If a plugin is named `lock` and is compiled/installed:
* **Automatic Decrypt on Open**: When a user presses `Enter` on a note, `blob` checks if the file starts with the signature `--- BLOB CRYPT V1 ---`. If it does, `blob` automatically executes `lock` on it. If `lock` successfully decrypts the note, `blob` opens it in the configured editor.
* **Automatic Encrypt on Exit**: As soon as the editor closes, `blob` immediately runs `lock` on the note again to ensure it is re-encrypted before writing to disk.

---

## 5. C Plugin Template

Here is a minimal C template to start writing your own `blob` plugin:

```c
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Error: No note path provided.\n");
        return 1;
    }

    const char *note_path = argv[1];
    printf("Processing note: %s\n", note_path);

    // 1. Read note_path content
    FILE *f = fopen(note_path, "r");
    if (!f) {
        fprintf(stderr, "Error: Could not open note file.\n");
        return 1;
    }
    // ... read file ...
    fclose(f);

    // 2. Perform your operation (e.g., encryption, tags, backup)
    
    // 3. Write back changes if necessary
    
    printf("Operation completed successfully!\n");
    return 0; // Return 0 to signal success to blob
}
```

---

## 6. How to Build & Publish

To distribute your addon:
1. Write the source `.c` and `README.md` manifest.
2. Submit a PR or add it to the remote repository.
3. Users will see it in the remote repository list when they press `p`, and `blob` will download and compile it on-device automatically!
