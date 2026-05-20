#!/usr/bin/env python3
"""blob — minimal notepad."""

import os, sys, json, subprocess, shutil
from pathlib import Path

# ── paths ─────────────────────────────────────────────────────────────────────
if sys.platform == "win32":
    DATA_DIR = Path.home() / "AppData" / "Local" / "blob"
else:
    DATA_DIR = Path.home() / ".local" / "share" / "blob"

NOTES_DIR = DATA_DIR / "notes"
CONFIG_FILE = DATA_DIR / "config.json"

if sys.platform == "win32":
    DEFAULT_EDITOR = "nvim"
else:
    DEFAULT_EDITOR = "vim"

DEFAULT_CONFIG = {"editor": DEFAULT_EDITOR, "theme": {"accent": "cyan"}}

# ── ANSI ──────────────────────────────────────────────────────────────────────
# enable VT on windows
if sys.platform == "win32":
    import ctypes

    kernel32 = ctypes.windll.kernel32
    kernel32.SetConsoleMode(kernel32.GetStdHandle(-11), 7)

ANSI = {
    "reset": "\033[0m",
    "bold": "\033[1m",
    "dim": "\033[2m",
    "cyan": "\033[96m",
    "green": "\033[92m",
    "yellow": "\033[93m",
    "magenta": "\033[95m",
    "blue": "\033[94m",
    "white": "\033[97m",
    "red": "\033[91m",
    "black": "\033[30m",
    "bg_cyan": "\033[46m",
    "bg_green": "\033[42m",
    "bg_yellow": "\033[43m",
    "bg_magenta": "\033[45m",
    "bg_blue": "\033[44m",
    "bg_white": "\033[47m",
    "hide_cursor": "\033[?25l",
    "show_cursor": "\033[?25h",
}

BG_MAP = {
    "cyan": "bg_cyan",
    "green": "bg_green",
    "yellow": "bg_yellow",
    "magenta": "bg_magenta",
    "blue": "bg_blue",
    "white": "bg_white",
}


def a(*keys):
    return "".join(ANSI.get(k, "") for k in keys)


def clear():
    os.system("cls" if sys.platform == "win32" else "clear")


# ── raw input ─────────────────────────────────────────────────────────────────
if sys.platform == "win32":
    import msvcrt

    def getch():
        ch = msvcrt.getwch()
        if ch in ("\x00", "\xe0"):
            return {"H": "UP", "P": "DOWN", "K": "LEFT", "M": "RIGHT"}.get(
                msvcrt.getwch(), ""
            )
        if ch == "\r":
            return "ENTER"
        if ch == "\x03":
            raise KeyboardInterrupt
        return ch
else:
    import tty, termios

    def getch():
        fd = sys.stdin.fileno()
        old = termios.tcgetattr(fd)
        try:
            tty.setraw(fd)
            ch = sys.stdin.read(1)
            if ch == "\x1b":
                ch2 = sys.stdin.read(1)
                ch3 = sys.stdin.read(1)
                return {"A": "UP", "B": "DOWN", "C": "RIGHT", "D": "LEFT"}.get(ch3, "")
            if ch in ("\r", "\n"):
                return "ENTER"
            if ch == "\x03":
                raise KeyboardInterrupt
            return ch
        finally:
            termios.tcsetattr(fd, termios.TCSADRAIN, old)


# ── config ────────────────────────────────────────────────────────────────────
def load_config():
    if CONFIG_FILE.exists():
        try:
            cfg = json.loads(CONFIG_FILE.read_text())
            for k, v in DEFAULT_CONFIG.items():
                if k not in cfg:
                    cfg[k] = v
                elif isinstance(v, dict):
                    for kk, vv in v.items():
                        cfg[k].setdefault(kk, vv)
            return cfg
        except Exception:
            pass
    return dict(DEFAULT_CONFIG)


def save_config(cfg):
    CONFIG_FILE.write_text(json.dumps(cfg, indent=2))


# ── notes ─────────────────────────────────────────────────────────────────────
def ensure_dirs():
    NOTES_DIR.mkdir(parents=True, exist_ok=True)
    if not CONFIG_FILE.exists():
        save_config(DEFAULT_CONFIG)


def list_notes():
    if not NOTES_DIR.exists():
        return []
    return sorted(NOTES_DIR.glob("*.md"), key=lambda p: p.stat().st_mtime, reverse=True)


def sanitize(name):
    name = name.strip().replace("/", "-").replace("\\", "-")
    return name if name.endswith(".md") else name + ".md"


# ── editor ────────────────────────────────────────────────────────────────────
def open_note(path, editor):
    sys.stdout.write(a("show_cursor"))
    sys.stdout.flush()

    if not shutil.which(editor):
        clear()
        print(f"\n  {a('red')}blob: '{editor}' not found in PATH.{a('reset')}")
        print(f"  Edit your config: {CONFIG_FILE}")
        print(f'  Set "editor" to nvim, notepad, code, etc.\n')
        input("  press enter...")
        return

    clear()
    if sys.platform == "win32":
        # wait=True so we get the terminal back after editor closes
        subprocess.run([editor, str(path)])
    else:
        subprocess.call([editor, str(path)])


# ── render ────────────────────────────────────────────────────────────────────
def render(notes, sel, cfg, status=""):
    accent = cfg["theme"].get("accent", "cyan")
    bg = BG_MAP.get(accent, "bg_cyan")

    clear()
    print(
        f"  {a('bold', accent)}blob{a('reset')}  {a('dim')}{cfg['editor']}{a('reset')}\n"
    )

    if not notes:
        print(f"  {a('dim')}no notes — press n to create one{a('reset')}")
    else:
        for i, note in enumerate(notes):
            name = note.stem
            if i == sel:
                print(f"  {a('bold', bg, 'black')} {name} {a('reset')}")
            else:
                print(f"  {a('dim')}·{a('reset')} {name}")

    print(f"\n  {a('dim')}↑↓ navigate  enter:open  n:new  d:delete  q:quit{a('reset')}")

    if status:
        print(f"\n  {a('red')}{status}{a('reset')}")


# ── main ──────────────────────────────────────────────────────────────────────
def main():
    ensure_dirs()
    cfg = load_config()
    notes = list_notes()
    sel = 0
    status = ""

    sys.stdout.write(a("hide_cursor"))
    sys.stdout.flush()

    try:
        while True:
            sel = max(0, min(sel, len(notes) - 1)) if notes else 0
            render(notes, sel, cfg, status)
            status = ""

            key = getch()

            if key in ("q", "Q"):
                break

            elif key == "UP":
                sel = max(0, sel - 1)

            elif key == "DOWN":
                sel = min(len(notes) - 1, sel + 1) if notes else 0

            elif key == "ENTER":
                if notes:
                    open_note(notes[sel], cfg["editor"])
                    notes = list_notes()

            elif key in ("n", "N", "+"):
                render(notes, sel, cfg)
                sys.stdout.write(a("show_cursor"))
                sys.stdout.flush()
                name = input("  new note name: ").strip()
                sys.stdout.write(a("hide_cursor"))
                sys.stdout.flush()
                if name:
                    path = NOTES_DIR / sanitize(name)
                    if path.exists():
                        status = f"'{name}' already exists"
                    else:
                        path.touch()
                        open_note(path, cfg["editor"])
                        notes = list_notes()
                        try:
                            sel = next(i for i, n in enumerate(notes) if n.stem == name)
                        except StopIteration:
                            sel = 0

            elif key in ("d", "D"):
                if notes:
                    name = notes[sel].stem
                    render(notes, sel, cfg)
                    sys.stdout.write(a("show_cursor"))
                    sys.stdout.flush()
                    confirm = input(f"  delete '{name}'? [y/N]: ").strip().lower()
                    sys.stdout.write(a("hide_cursor"))
                    sys.stdout.flush()
                    if confirm == "y":
                        notes[sel].unlink(missing_ok=True)
                        notes = list_notes()
                        sel = max(0, sel - 1)

    except KeyboardInterrupt:
        pass
    finally:
        sys.stdout.write(a("show_cursor", "reset") + "\n")
        sys.stdout.flush()


if __name__ == "__main__":
    main()
