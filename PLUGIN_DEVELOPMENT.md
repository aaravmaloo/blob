# `blob` plugin development

`blob` supports small compiled plugins. Plugins are standalone executables that receive either a selected note path or the notes directory as `argv[1]`.

---

## 1. Directory Structure

A plugin must live in its own folder under `addons/`:

```text
addons/
└── my_plugin/
    ├── my_plugin.c
    └── README.md
```

The folder name and C file name should match the plugin name.

On compile:

```text
POSIX:   addons/my_plugin/my_plugin
Windows: addons/my_plugin/my_plugin.exe
```

---

## 2. Manifest

`blob` reads plugin metadata from the top of `README.md`.

```md
# my_plugin

[api] = 2
[version] = 1.0.0
[authors] = {"Your Name"}
[description] = <What this plugin does.>
[keybind] = x
[mode] = note
[permissions] = {"read-note"}
```

Fields:

- `# my_plugin`: Plugin name. Should match the folder name.
- `[api] = 2`: Current plugin API. Missing means legacy API 1.
- `[version] = 1.0.0`: Plugin version.
- `[authors] = {...}`: Author list.
- `[description] = <...>`: Short text shown in the plugin manager.
- `[keybind] = x`: Shortcut from the notes list.
- `[mode] = note`: Plugin receives the selected note path.
- `[mode] = workspace`: Plugin receives the notes directory.
- `[permissions] = {...}`: Declared capabilities shown before install/run.

Reserved core keys:

```text
n r d D y / p : q Enter Esc arrows
```

Do not use these for plugins. If two plugins use the same key, blob marks both as conflicted.

---

## 3. Permissions

Permissions are user-facing safety labels. They do not sandbox the executable. They tell users what the plugin intends to do before install and run.

Existing permissions:

| Permission | Use when |
|------------|----------|
| `read-note` | Reading the selected note file. |
| `write-note` | Editing the selected note content. |
| `move-note` | Renaming, moving, archiving, pinning, or trashing a note. |
| `read-notes` | Reading multiple notes or scanning the notes directory. |
| `write-notes` | Editing multiple notes or writing generated files into the notes directory. |
| `network` | Accessing the internet or a remote service. |
| `run-git` | Running `git` commands. |
| `sensitive-input` | Asking for passwords, tokens, keys, or private data. |

Pick the smallest honest set. Examples:

```md
[permissions] = {"read-note"}
[permissions] = {"read-note","write-note"}
[permissions] = {"network","read-notes","write-notes","run-git"}
```

Custom permissions are allowed. Use short kebab-case names:

```md
[permissions] = {"read-note","export-pdf"}
```

Add a custom permission when existing permissions are too vague. If the action already fits `write-note`, `network`, or another existing permission, use the existing permission.


## 4. Invocation

For note plugins:

```bash
addons/my_plugin/my_plugin "/path/to/notes/my-note.md"
```

For workspace plugins:

```bash
addons/fuzzy-search/fuzzy-search "/path/to/notes"
```

Before running a plugin, blob clears its terminal region, disables raw terminal mode, then waits for the plugin to exit. Your plugin can use normal stdin/stdout.

---

## 5. Special Core Hook: `lock`

If a compiled plugin is named `lock`, blob can use it while opening encrypted notes.

- On open: blob runs `lock` to decrypt before launching the editor.
- After editor exit: blob runs `lock` again to re-encrypt.

The lock plugin should declare:

```md
[mode] = note
[permissions] = {"read-note","write-note","sensitive-input"}
```

---

## 6. Minimal Note Plugin

```c
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <note_path>\n", argv[0]);
        return 1;
    }

    const char *note_path = argv[1];
    printf("Selected note: %s\n", note_path);

    return 0;
}
```

---

## 7. Publishing

1. Create `addons/my_plugin/my_plugin.c`.
2. Create `addons/my_plugin/README.md` with an API 2 manifest.
3. Add `my_plugin` to `addons/addons.txt`.
4. Submit the plugin.

Users will see it in the plugin manager and can compile it locally.
