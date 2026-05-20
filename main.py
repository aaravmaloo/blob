"""
blob — a minimal notepad that stays outta your way.
Data dir: ~/appdata/blob/
"""

import os
import sys
import json
import subprocess
import shutil
from pathlib import Path

try:
    import curses
except ImportError:
    # Windows doesn't ship curses — needs windows-curses
    try:
        import windows_curses as curses  # type: ignore

        sys.modules["curses"] = curses
    except ImportError:
        print("missing dependency: run  pip install windows-curses")
        sys.exit(1)

# ── paths ────────────────────────────────────────────────────────────────────
if sys.platform == "win32":
    DATA_DIR = Path.home() / "AppData" / "Local" / "blob"
else:
    DATA_DIR = Path.home() / ".local" / "share" / "blob"
NOTES_DIR = DATA_DIR / "notes"
CONFIG_FILE = DATA_DIR / "config.json"

# ── default config ────────────────────────────────────────────────────────────
DEFAULT_CONFIG = {
    "editor": "vim",
    "theme": {
        "title_fg": "cyan",
        "title_bold": True,
        "item_fg": "white",
        "selected_fg": "black",
        "selected_bg": "cyan",
        "hint_fg": "bright_black",
        "border_fg": "cyan",
    },
}

# curses color name → curses constant
COLOR_MAP = {
    "black": curses.COLOR_BLACK,
    "red": curses.COLOR_RED,
    "green": curses.COLOR_GREEN,
    "yellow": curses.COLOR_YELLOW,
    "blue": curses.COLOR_BLUE,
    "magenta": curses.COLOR_MAGENTA,
    "cyan": curses.COLOR_CYAN,
    "white": curses.COLOR_WHITE,
    "bright_black": 8,  # most terminals support 256 colors
}


# ── config helpers ────────────────────────────────────────────────────────────
def load_config() -> dict:
    if CONFIG_FILE.exists():
        try:
            with open(CONFIG_FILE) as f:
                cfg = json.load(f)
            # merge missing keys from default
            for k, v in DEFAULT_CONFIG.items():
                if k not in cfg:
                    cfg[k] = v
                elif isinstance(v, dict):
                    for kk, vv in v.items():
                        if kk not in cfg[k]:
                            cfg[k][kk] = vv
            return cfg
        except Exception:
            pass
    return dict(DEFAULT_CONFIG)


def save_config(cfg: dict):
    CONFIG_FILE.write_text(json.dumps(cfg, indent=2))


# ── note helpers ──────────────────────────────────────────────────────────────
def ensure_dirs():
    NOTES_DIR.mkdir(parents=True, exist_ok=True)
    if not CONFIG_FILE.exists():
        save_config(DEFAULT_CONFIG)


def list_notes() -> list[Path]:
    if not NOTES_DIR.exists():
        return []
    notes = sorted(
        NOTES_DIR.glob("*.md"), key=lambda p: p.stat().st_mtime, reverse=True
    )
    return notes


def note_path(name: str) -> Path:
    if not name.endswith(".md"):
        name += ".md"
    return NOTES_DIR / name


def open_note(path: Path, editor: str):
    """Open note in editor, suspending curses."""
    curses.endwin()
    if not shutil.which(editor):
        print(f"\n  blob: editor '{editor}' not found.")
        print(f"  Install it or set a different editor in {CONFIG_FILE}\n")
        input("  press enter to continue...")
        return
    subprocess.call([editor, str(path)])


def delete_note(path: Path):
    path.unlink(missing_ok=True)


# ── curses theme setup ────────────────────────────────────────────────────────
PAIR_TITLE = 1
PAIR_ITEM = 2
PAIR_SELECTED = 3
PAIR_HINT = 4
PAIR_BORDER = 5
PAIR_DANGER = 6


def init_colors(theme: dict):
    curses.start_color()
    curses.use_default_colors()

    def c(name):
        return COLOR_MAP.get(name, curses.COLOR_WHITE)

    curses.init_pair(PAIR_TITLE, c(theme["title_fg"]), -1)
    curses.init_pair(PAIR_ITEM, c(theme["item_fg"]), -1)
    curses.init_pair(PAIR_SELECTED, c(theme["selected_fg"]), c(theme["selected_bg"]))
    curses.init_pair(PAIR_HINT, c(theme["hint_fg"]), -1)
    curses.init_pair(PAIR_BORDER, c(theme["border_fg"]), -1)
    curses.init_pair(PAIR_DANGER, curses.COLOR_RED, -1)


# ── draw helpers ──────────────────────────────────────────────────────────────
def safe_addstr(win, y, x, text, attr=0):
    h, w = win.getmaxyx()
    if y < 0 or y >= h:
        return
    max_len = w - x - 1
    if max_len <= 0:
        return
    try:
        win.addstr(y, x, text[:max_len], attr)
    except curses.error:
        pass


def draw_border(win, theme):
    attr = curses.color_pair(PAIR_BORDER)
    h, w = win.getmaxyx()
    try:
        win.border()
    except curses.error:
        pass


def draw_ui(win, notes: list[Path], sel: int, offset: int, cfg: dict, status: str = ""):
    win.erase()
    h, w = win.getmaxyx()
    theme = cfg["theme"]

    draw_border(win, theme)

    # title
    title = " blob "
    title_attr = curses.color_pair(PAIR_TITLE)
    if theme.get("title_bold"):
        title_attr |= curses.A_BOLD
    safe_addstr(win, 0, max(2, (w - len(title)) // 2), title, title_attr)

    # editor hint (top right)
    editor_hint = f" {cfg['editor']} "
    safe_addstr(
        win, 0, w - len(editor_hint) - 2, editor_hint, curses.color_pair(PAIR_HINT)
    )

    list_top = 2
    list_bottom = h - 3
    visible = list_bottom - list_top

    if not notes:
        msg = "no notes yet — press n to create one"
        safe_addstr(
            win, h // 2, max(1, (w - len(msg)) // 2), msg, curses.color_pair(PAIR_HINT)
        )
    else:
        for i, note in enumerate(notes[offset : offset + visible]):
            idx = offset + i
            row = list_top + i
            name = note.stem
            line = f"  {name}"

            if idx == sel:
                attr = curses.color_pair(PAIR_SELECTED) | curses.A_BOLD
                safe_addstr(
                    win, row, 1, " " * (w - 2), curses.color_pair(PAIR_SELECTED)
                )
                safe_addstr(win, row, 1, line, attr)
            else:
                safe_addstr(win, row, 1, line, curses.color_pair(PAIR_ITEM))

    # bottom hints
    hints = " n:new  enter:open  d:delete  q:quit "
    safe_addstr(
        win, h - 2, max(1, (w - len(hints)) // 2), hints, curses.color_pair(PAIR_HINT)
    )

    # status bar
    if status:
        safe_addstr(
            win,
            h - 1,
            2,
            status[: w - 4],
            curses.color_pair(PAIR_DANGER) | curses.A_BOLD,
        )

    win.refresh()


# ── input prompt ─────────────────────────────────────────────────────────────
def prompt_input(win, prompt: str) -> str:
    h, w = win.getmaxyx()
    row = h - 2
    curses.echo()
    curses.curs_set(1)
    win.move(row, 1)
    win.clrtoeol()
    safe_addstr(win, row, 1, prompt, curses.color_pair(PAIR_TITLE) | curses.A_BOLD)
    win.refresh()
    try:
        val = (
            win.getstr(row, 1 + len(prompt), w - len(prompt) - 4)
            .decode("utf-8")
            .strip()
        )
    except Exception:
        val = ""
    curses.noecho()
    curses.curs_set(0)
    return val


# ── confirm prompt ────────────────────────────────────────────────────────────
def confirm(win, msg: str) -> bool:
    h, w = win.getmaxyx()
    row = h - 2
    win.move(row, 1)
    win.clrtoeol()
    full = f"{msg} [y/N] "
    safe_addstr(win, row, 1, full, curses.color_pair(PAIR_DANGER) | curses.A_BOLD)
    win.refresh()
    ch = win.getch()
    return ch in (ord("y"), ord("Y"))


# ── main TUI loop ─────────────────────────────────────────────────────────────
def main(win):
    ensure_dirs()
    cfg = load_config()
    theme = cfg["theme"]

    curses.curs_set(0)
    init_colors(theme)
    win.keypad(True)

    notes = list_notes()
    sel = 0
    offset = 0
    status = ""

    while True:
        h, _ = win.getmaxyx()
        visible = max(1, h - 5)

        # clamp sel
        if notes:
            sel = max(0, min(sel, len(notes) - 1))
            if sel < offset:
                offset = sel
            elif sel >= offset + visible:
                offset = sel - visible + 1
        else:
            sel = offset = 0

        draw_ui(win, notes, sel, offset, cfg, status)
        status = ""

        key = win.getch()

        if key in (ord("q"), ord("Q")):
            break

        elif key == curses.KEY_UP:
            if notes:
                sel = max(0, sel - 1)

        elif key == curses.KEY_DOWN:
            if notes:
                sel = min(len(notes) - 1, sel + 1)

        elif key in (curses.KEY_ENTER, 10, 13):
            if notes:
                open_note(notes[sel], cfg["editor"])
                # re-init after editor exits
                curses.curs_set(0)
                init_colors(cfg["theme"])
                win.keypad(True)
                notes = list_notes()

        elif key in (ord("n"), ord("N")):
            name = prompt_input(win, "note name: ")
            if name:
                # sanitize — no slashes
                name = name.replace("/", "-").replace("\\", "-")
                path = note_path(name)
                if path.exists():
                    status = f"note '{name}' already exists"
                else:
                    path.touch()
                    open_note(path, cfg["editor"])
                    curses.curs_set(0)
                    init_colors(cfg["theme"])
                    win.keypad(True)
                notes = list_notes()
                # put cursor on the new note
                try:
                    sel = next(i for i, n in enumerate(notes) if n.stem == name)
                except StopIteration:
                    sel = 0

        elif key in (ord("d"), ord("D")):
            if notes:
                name = notes[sel].stem
                if confirm(win, f"delete '{name}'?"):
                    delete_note(notes[sel])
                    notes = list_notes()
                    sel = max(0, sel - 1)


# ── entry point ───────────────────────────────────────────────────────────────
if __name__ == "__main__":
    try:
        curses.wrapper(main)
    except KeyboardInterrupt:
        pass
