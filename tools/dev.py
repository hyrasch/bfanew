#!/usr/bin/env python3
"""
dev.py - one-command iteration loop for bfanew (Windows).

    python tools/dev.py build [--configure]   stop the server, build it (VS dev shell + Ninja)
    python tools/dev.py serve                 run the server in the foreground (use in a terminal tab)
    python tools/dev.py restart               stop the running server, start it detached
    python tools/dev.py stop
    python tools/dev.py reload                GET /bfanew/reload: re-read deploy/system/*.json, no restart
    python tools/dev.py bump M_GACHA_MST      bump that table's version in version_info.json (+reload)
    python tools/dev.py client [--to home|summon] [--fresh]
                                              relaunch the game on the device and drive it there
    python tools/dev.py summon [--rare] [--multi]   from the Summon scene: press Summon, confirm, open the door
    python tools/dev.py shot [name]           screenshot -> build/shots/<name>.png
    python tools/dev.py tap X Y | tapk NAME | swipe X1 Y1 X2 Y2 | key back|home
    python tools/dev.py logs [-n 15] [--show ID]   latest server requests with handler names, errors flagged
    python tools/dev.py loop [--to summon]    build + restart + client, the whole round

Device: --device SERIAL (default 127.0.0.1:6610, BlueStacks with ADB enabled). Screen
coordinates assume BlueStacks at 1920x1080 with the game window centred (see COORDS).
The driver waits on the server's request log instead of fixed sleeps.
"""
from __future__ import annotations

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SERVER = ROOT / "server"
DEPLOY = SERVER / "deploy"
LOGDIR = DEPLOY / "log"
EXE = SERVER / "out/build/debug-win64-ninja/standalone_frontend/gimuserverw.exe"
SHOTS = ROOT.parent / "build" / "shots"
PKG = "sg.gumi.bravefrontier"
SERVER_URL = "http://127.0.0.1:9960"
VSDEVCMD = r"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"

# Tap targets as fractions of the game's CONTENT area. The window frame comes from
# `dumpsys window` at runtime; inside it the game draws a 2:3 (640x960) area, letterboxed
# when the frame is taller than ~16:9, filling it otherwise. Re-measure with `shot` if the
# layout changes (the game re-lays out some elements for other aspects).
COORDS = {
    "title": (0.50, 0.50),
    "ok": (0.50, 0.585),            # popup OK (both the small "Unable to handle" and the taller ones)
    "tab_home": (0.088, 0.969),
    "tab_unit": (0.255, 0.969),
    "tab_town": (0.416, 0.969),
    "tab_shop": (0.582, 0.969),
    "tab_summon": (0.745, 0.969),
    "tab_social": (0.912, 0.969),
    "banner_summon": (0.499, 0.617),  # Summon button on the banner page
    "dlg_summon": (0.279, 0.758),     # single summon in the confirmation dialog
    "dlg_multi": (0.724, 0.758),
    "door": (0.499, 0.512),           # summon door / result screen tap-through
    "popup_close": (0.311, 0.843),
    "popup_claim": (0.499, 0.843),
    "back": (0.078, 0.219),           # the scene's Back button (top-left of the sub-header)
    "unit_view": (0.266, 0.375),      # Unit menu
    "unit_squad": (0.734, 0.375),
    "unit_fusion": (0.266, 0.543),
    "unit_evolve": (0.734, 0.543),
    "unit_sphere": (0.266, 0.715),
    "unit_sell": (0.734, 0.715),
    "terms_checkbox": (0.142, 0.696),
    "terms_agree": (0.499, 0.854),
    "phone_ok": (0.499, 0.658),
}

DEVICE = os.environ.get("BF_DEVICE", "127.0.0.1:6610")


# ----------------------------------------------------------------------------- helpers
def adb_path() -> str:
    p = shutil.which("adb")
    if p:
        return p
    for c in glob.glob(os.path.expandvars(r"%LOCALAPPDATA%\Microsoft\WinGet\Packages\Google.PlatformTools*\platform-tools\adb.exe")):
        return c
    sys.exit("adb not found (install Google.PlatformTools via winget or put adb on PATH)")


def adb(*args: str, capture: bool = True, check: bool = False) -> str:
    cmd = [adb_path(), "-s", DEVICE, *args]
    r = subprocess.run(cmd, capture_output=capture, text=True)
    if check and r.returncode != 0:
        sys.exit(f"adb failed: {' '.join(args)}\n{r.stdout}{r.stderr}")
    return (r.stdout or "") if capture else ""


def connect():
    if ":" in DEVICE:
        out = subprocess.run([adb_path(), "connect", DEVICE], capture_output=True, text=True).stdout
        if "connected" not in out and "already" not in out:
            sys.exit(f"cannot connect to {DEVICE}: {out.strip()} (enable ADB in BlueStacks settings > Advanced)")


def focus() -> str:
    out = adb("shell", "dumpsys", "window")
    m = re.search(r"mCurrentFocus=Window\{\S+ u0 (\S+)", out)
    return m.group(1) if m else ""


def game_alive() -> bool:
    return adb("shell", "pidof", PKG).strip() != ""


_FRAME: tuple[int, int, int, int] | None = None


def frame() -> tuple[int, int, int, int]:
    """(x0, y0, w, h) of the game's window content on the display."""
    global _FRAME
    out = adb("shell", "dumpsys", "window", "windows")
    i = out.find("u0 " + PKG + "/")
    m = re.search(r"frame=\[(\d+),(\d+)\]\[(\d+),(\d+)\]", out[i:]) if i >= 0 else None
    if not m:
        if _FRAME:
            return _FRAME
        sys.exit("game window not found (is it running and in front?)")
    x0, y0, x1, y1 = map(int, m.groups())
    _FRAME = (x0, y0, x1 - x0, y1 - y0)
    return _FRAME


def content() -> tuple[int, int, int, int]:
    """The game's drawing area inside the frame: the whole frame for phone-like aspects,
    else the centred 2:3 rectangle (black bars above and below)."""
    x0, y0, w, h = frame()
    if h / w <= 1.8:
        return x0, y0, w, h
    ch = int(w * 1.5)
    return x0, y0 + (h - ch) // 2, w, ch


def to_px(fx: float, fy: float) -> tuple[int, int]:
    x0, y0, w, h = content()
    return int(x0 + fx * w), int(y0 + fy * h)


def tap(x: int, y: int):
    adb("shell", "input", "tap", str(x), str(y))


def tapk(key: str):
    tap(*to_px(*COORDS[key]))


def shot(name: str = "shot") -> Path:
    SHOTS.mkdir(parents=True, exist_ok=True)
    out = SHOTS / f"{name}.png"
    adb("shell", "screencap", "-p", "/sdcard/bf_dev_shot.png")
    adb("pull", "/sdcard/bf_dev_shot.png", str(out))
    return out


def dismiss_google():
    """The Google Play Games sign-in (gms MinuteMaid) pops on launch; Back dismisses it."""
    if "com.google.android.gms" in focus():
        adb("shell", "input", "keyevent", "4")
        return True
    return False


def handler_names() -> dict[str, str]:
    names = {}
    src = (SERVER / "gimuserver/gme/GmeControllerHandlers.cpp").read_text(encoding="utf-8", errors="replace")
    for m in re.finditer(r'REGISTER\("(\w+)",\s*(\w+)', src):
        names[m.group(1)] = m.group(2)
    names.update({"DLS": "DLS", "guestLogin": "guestLogin", "webpage": "content"})
    return names


def log_files() -> list[Path]:
    return sorted(LOGDIR.glob("http_log_*.log"), key=lambda p: p.stat().st_mtime)


def log_id(p: Path) -> str:
    m = re.match(r"http_log_(\w+?)_\d{8}_\d{6}\.log", p.name)
    return m.group(1) if m else "?"


def wait_for_log(ids: set[str], since: float, timeout: float, on_tick=None) -> Path | None:
    """Wait until a request log with one of `ids` newer than `since` exists."""
    t0 = time.time()
    while time.time() - t0 < timeout:
        for p in reversed(log_files()):
            if p.stat().st_mtime <= since:
                break
            if log_id(p) in ids:
                return p
        if on_tick:
            on_tick()
        time.sleep(1)
    return None


# ----------------------------------------------------------------------------- server
def server_pids() -> list[str]:
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq gimuserverw.exe", "/FO", "CSV", "/NH"], capture_output=True, text=True).stdout
    return [line.split('","')[1] for line in out.splitlines() if line.startswith('"gimuserverw.exe"')]


def cmd_stop(_):
    for pid in server_pids():
        subprocess.run(["taskkill", "/PID", pid, "/F"], capture_output=True)
        print(f"stopped gimuserverw pid {pid}")


def cmd_start(_):
    if not EXE.exists():
        sys.exit(f"{EXE} missing, run: python tools/dev.py build --configure")
    subprocess.Popen([str(EXE)], cwd=DEPLOY, creationflags=subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(20):
        time.sleep(0.5)
        if ping():
            print(f"server up: {SERVER_URL}/dls ok (pids {server_pids()})")
            return
    sys.exit("server did not answer /dls within 10 s")


def ping() -> bool:
    try:
        with urllib.request.urlopen(SERVER_URL + "/dls", timeout=2) as r:
            return r.status == 200
    except Exception:
        return False


def cmd_restart(a):
    cmd_stop(a)
    time.sleep(0.5)
    cmd_start(a)


def cmd_serve(_):
    """Foreground server for a terminal tab; Ctrl-C stops it."""
    cmd_stop(None)
    print(f"running {EXE.name} from {DEPLOY} (Ctrl-C to stop)")
    os.chdir(DEPLOY)
    sys.exit(subprocess.call([str(EXE)]))


def cmd_build(a):
    if not Path(VSDEVCMD).exists():
        sys.exit(f"VsDevCmd.bat not found at {VSDEVCMD}")
    if not getattr(a, "keep", False):
        cmd_stop(None)  # the link step needs the exe free; a `serve` tab shows the exit and can be re-run
    steps = []
    if a.configure:
        steps.append("cmake --preset debug-win64-ninja")
    steps.append("cmake --build --preset debug-win64-ninja")
    line = f'call "{VSDEVCMD}" -arch=amd64 -no_logo >nul && ' + " && ".join(steps)
    print("$", " && ".join(steps))
    r = subprocess.run(line, shell=True, cwd=SERVER)  # one string: cmd.exe must see the quotes as-is
    if r.returncode != 0:
        sys.exit(f"build failed ({r.returncode}); if a .cpp was added, use --configure; if the exe is locked, run stop first")
    print("build ok")


def cmd_reload(_):
    try:
        with urllib.request.urlopen(SERVER_URL + "/bfanew/reload", timeout=10) as r:
            print(r.read().decode().strip())
    except Exception as ex:
        sys.exit(f"reload failed: {ex} (server running? built with DevController?)")


def cmd_bump(a):
    import json
    p = DEPLOY / "system/version_info.json"
    doc = json.loads(p.read_text(encoding="utf-8"))
    rows = doc[next(iter(doc))]
    hit = [r for r in rows if r.get("moWQ30GH") == a.table]
    if not hit:
        sys.exit(f"{a.table} not in version_info.json")
    old = hit[0]["d2RFtP8T"]
    hit[0]["d2RFtP8T"] = str(int(old) + 1)
    p.write_text(json.dumps(doc, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"{a.table}: {old} -> {hit[0]['d2RFtP8T']}")
    cmd_reload(a)


# ----------------------------------------------------------------------------- client
def launch_game(fresh: bool):
    adb("shell", "am", "force-stop", PKG)
    if fresh:
        adb("shell", "pm", "clear", PKG)
    adb("shell", "monkey", "-p", PKG, "-c", "android.intent.category.LAUNCHER", "1")


def drive_to_home(fresh: bool, verbose=True) -> bool:
    """Launch and get to the home screen. Returns False if the game died."""
    since = time.time()
    launch_game(fresh)
    say = print if verbose else (lambda *a, **k: None)

    if fresh:
        # Terms and conditions, then the phone-permission notice, then the MST download.
        say("fresh install: terms + permission + MST download")
        time.sleep(12)
        tapk("terms_checkbox"); time.sleep(1); tapk("terms_agree"); time.sleep(4)
        tapk("phone_ok")
        if not wait_for_log({"DLS"}, since, 240, dismiss_google):
            say("no DLS request after the fresh-install flow"); return False
    elif not wait_for_log({"DLS"}, since, 90, dismiss_google):
        say("no DLS request within 90 s: is the server up and the APK pointed at it?"); return False
    say("DLS reached, waiting for the title screen")

    # Title screen needs a tap; tapping early is harmless, so tap every 2 s until the guest login shows up.
    t0 = time.time()
    while time.time() - t0 < 120:
        dismiss_google()
        tapk("title")
        if wait_for_log({"guestLogin"}, since, 2):
            break
    else:
        say("title tap never produced the guest login"); return False
    say("logging in")

    if not wait_for_log({"NiYWKdzs"}, since, 90, dismiss_google):  # HomeInfo
        say("HomeInfo never came (crash during Initialize/UserInfo?)"); return False
    time.sleep(3)
    dismiss_google()
    # Startup popups (daily tasks, login campaign) are removed from the data; nothing to dismiss.
    if not game_alive():
        say("game process died"); return False
    say("home screen")
    return True


def cmd_client(a):
    connect()
    if not drive_to_home(a.fresh):
        shot("client_failed"); print(f"screenshot: {SHOTS / 'client_failed.png'}"); sys.exit(1)
    if a.to == "summon":
        tapk("tab_summon"); time.sleep(4); print("summon scene")
    elif a.to == "unit":
        tapk("tab_unit"); time.sleep(4); print("unit scene")
    p = shot(f"client_{a.to}")
    print(f"screenshot: {p}")


def cmd_summon(a):
    """From the Summon scene (first banner shown): press Summon, confirm, open the door, tap through."""
    connect()
    since = time.time()
    if a.rare:
        x1, y1 = to_px(0.85, 0.45); x2, y2 = to_px(0.15, 0.45)
        adb("shell", "input", "swipe", str(x1), str(y1), str(x2), str(y2), "400"); time.sleep(3)
    tapk("banner_summon"); time.sleep(4)
    tapk("dlg_multi" if a.multi else "dlg_summon")
    p = wait_for_log({"F7JvPk5H"}, since, 30)
    if not p:
        shot("summon_failed"); sys.exit("no GachaAction request; is the dialog open? see build/shots/summon_failed.png")
    body = p.read_text(encoding="utf-8", errors="replace")
    if "RESPONSE IN ERROR" in body:
        sys.exit("GachaAction answered an error:\n" + body[body.index("RESPONSE"):][:300])
    units = re.findall(r'"pn16CNah":"(\d+)"', body)
    print(f"summoned unit ids: {units[1:] if len(units) > 1 else units}")
    time.sleep(6)
    tapk("door"); time.sleep(6)       # open the door
    shot("summon_result"); print(f"screenshot: {SHOTS / 'summon_result.png'}")
    tapk("door"); time.sleep(4)       # acquired -> unit detail
    tapk("door"); time.sleep(3)       # unit detail -> back to summon scene
    print("alive" if game_alive() else "GAME DIED")


def cmd_shot(a):
    connect()
    print(shot(a.name), "frame =", frame(), "content =", content())


def cmd_tap(a):
    connect(); tap(a.x, a.y)


def cmd_tapk(a):
    connect(); print(a.key, "->", to_px(*COORDS[a.key])); tapk(a.key)


def cmd_swipe(a):
    connect(); adb("shell", "input", "swipe", str(a.x1), str(a.y1), str(a.x2), str(a.y2), "400")


def cmd_key(a):
    connect(); adb("shell", "input", "keyevent", {"back": "4", "home": "3"}[a.key])


def cmd_logs(a):
    names = handler_names()
    files = log_files()
    if a.show:
        for p in reversed(files):
            if log_id(p) == a.show:
                print(p.read_text(encoding="utf-8", errors="replace")[: a.chars]); return
        sys.exit(f"no log for {a.show}")
    for p in files[-a.n:]:
        body = p.read_text(encoding="utf-8", errors="replace")
        lid = log_id(p)
        name = names.get(lid, "UNKNOWN " + lid)
        extra = ""
        if lid == "webpage":
            m = re.search(r"request dump: (\S+)", body); extra = m.group(1) if m else ""
        err = "  <-- ERROR" if "RESPONSE IN ERROR" in body or "Deserialization" in body else ""
        t = p.name.split("_")[-1][:6]
        print(f"{t[:2]}:{t[2:4]}:{t[4:]}  {name:28s} {extra}{err}")


def cmd_loop(a):
    cmd_build(a); cmd_restart(a); cmd_client(a)


# ----------------------------------------------------------------------------- main
def main():
    global DEVICE
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--device", default=DEVICE)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build"); b.add_argument("--configure", action="store_true"); b.add_argument("--keep", action="store_true", help="do not stop a running server first"); b.set_defaults(f=cmd_build)
    sub.add_parser("serve").set_defaults(f=cmd_serve)
    sub.add_parser("start").set_defaults(f=cmd_start)
    sub.add_parser("stop").set_defaults(f=cmd_stop)
    sub.add_parser("restart").set_defaults(f=cmd_restart)
    sub.add_parser("reload").set_defaults(f=cmd_reload)
    p = sub.add_parser("bump"); p.add_argument("table"); p.set_defaults(f=cmd_bump)
    p = sub.add_parser("client"); p.add_argument("--to", default="home", choices=["home", "summon", "unit"]); p.add_argument("--fresh", action="store_true"); p.set_defaults(f=cmd_client)
    p = sub.add_parser("summon"); p.add_argument("--rare", action="store_true"); p.add_argument("--multi", action="store_true"); p.set_defaults(f=cmd_summon)
    p = sub.add_parser("shot"); p.add_argument("name", nargs="?", default="shot"); p.set_defaults(f=cmd_shot)
    p = sub.add_parser("tap"); p.add_argument("x", type=int); p.add_argument("y", type=int); p.set_defaults(f=cmd_tap)
    p = sub.add_parser("tapk"); p.add_argument("key", choices=sorted(COORDS)); p.set_defaults(f=cmd_tapk)
    p = sub.add_parser("swipe"); [p.add_argument(n, type=int) for n in ("x1", "y1", "x2", "y2")]; p.set_defaults(f=cmd_swipe)
    p = sub.add_parser("key"); p.add_argument("key", choices=["back", "home"]); p.set_defaults(f=cmd_key)
    p = sub.add_parser("logs"); p.add_argument("-n", type=int, default=15); p.add_argument("--show"); p.add_argument("--chars", type=int, default=3000); p.set_defaults(f=cmd_logs)
    p = sub.add_parser("loop"); p.add_argument("--to", default="home", choices=["home", "summon", "unit"]); p.add_argument("--fresh", action="store_true"); p.add_argument("--configure", action="store_true"); p.set_defaults(f=cmd_loop)
    a = ap.parse_args()
    DEVICE = a.device
    a.f(a)


if __name__ == "__main__":
    main()
