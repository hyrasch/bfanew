"""Extract APK assets that the served content needs but game_content lacks.

The content archive (21900.zip) is a mix of versions: the village animation
`content/sam/MapVillage/MapVillage.sam` is the 2.19.6 one, but its images in the archive are
older (`MapVillage_320x200.png` instead of `MapVillage_320x289.jpg`, `..._640x208_2.png` instead of
`..._640x384.png`). The client then downloads the missing images from the server, got the blank
1x1 PNG fallback, cached it, and the village scene died with a heap corruption on every visit
(see CLAUDE.md "Village crash"). The APK bundles the right files under `assets/`.

This script scans every `.sam` under game_content for referenced images, and copies those missing
from the content tree but present in the APK into `server/deploy/apk_assets/` (flat, by basename,
gitignored). `BfWebController` serves a missing `/content/...` file from there by basename before
falling back to the blank image. `--all` extracts every APK asset instead (large).

Usage: python tools/fill_content_from_apk.py [--apk ../brave-frontier-2-19-6-0.apk] [--all]
"""
import argparse
import os
import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEPLOY = ROOT / "server" / "deploy"
CONTENT = DEPLOY / "game_content" / "content"
OUT = DEPLOY / "apk_assets"
IMG_RE = re.compile(rb"[A-Za-z0-9_\-]{3,}\.(?:png|jpg|jpeg|plist|csv)")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--apk", default=str(ROOT.parent / "brave-frontier-2-19-6-0.apk"))
    ap.add_argument("--all", action="store_true", help="extract every APK asset")
    a = ap.parse_args()

    z = zipfile.ZipFile(a.apk)
    apk = {n[len("assets/"):]: n for n in z.namelist() if n.startswith("assets/") and not n.endswith("/")}
    have = {f for _, _, fs in os.walk(CONTENT) for f in fs}

    if a.all:
        wanted = sorted(apk)
    else:
        wanted = set()
        nsam = 0
        for d, _, fs in os.walk(CONTENT):
            for f in fs:
                if not f.endswith(".sam"):
                    continue
                nsam += 1
                data = (Path(d) / f).read_bytes()
                for m in IMG_RE.findall(data):
                    name = m.decode()
                    if name not in have and name in apk:
                        wanted.add(name)
                        print(f"{Path(d).relative_to(CONTENT) / f}: {name}")
        print(f"{nsam} sam files scanned, {len(wanted)} referenced files missing from content but in the APK")
        wanted = sorted(wanted)

    OUT.mkdir(parents=True, exist_ok=True)
    # a valid JPEG for missing .jpg requests (the 1x1 PNG fallback is wrong for a .jpg path: cocos
    # picks the decoder from the extension); the smallest JPEG of the content tree does
    blank = OUT / "_blank.jpg"
    if not blank.exists():
        src = min((Path(d) / f for d, _, fs in os.walk(CONTENT) for f in fs if f.lower().endswith((".jpg", ".jpeg"))),
                  key=lambda q: q.stat().st_size)
        blank.write_bytes(src.read_bytes())
        print(f"blank jpg: {src.relative_to(CONTENT)} ({src.stat().st_size} bytes)")
    n = 0
    for name in wanted:
        dst = OUT / name
        if dst.exists():
            continue
        dst.write_bytes(z.read(apk[name]))
        n += 1
    print(f"wrote {n} new files to {OUT} ({len(wanted)} wanted)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
