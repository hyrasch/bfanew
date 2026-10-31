#!/usr/bin/env python3
"""
build_overrides.py - build server/deploy/overrides/ (gitignored), the files BfWebController serves
before the content archive (deploy/game_content, a read-only clone of decompfrontier's).

Today that is the UI text table and the resource table that announces it:
  - content/sgtext/sgtext_en_<v>.zip: the archive's sgtext_en_<base> (a zip of one base64-encoded
    "KEY^en^th^id^vi^es^pt" CSV, first line "sgtext_en^^^^^^") plus / overriding the lines of
    data/sgtext_en_patch.csv ("KEY^text", the text is used for every language column).
    The client (TextDictionary_SG::DictionaryFileUpdateCheck) downloads a dictionary whose exact
    file name it does not have yet, so a text change needs a new version suffix;
  - mst/Ver<v>_hdt2N6mr_{1,2}.dat: F_RESOURCE_MST (the download list, see tools/mst.py) with the
    sgtext_en path pointing at the new version, re-encoded; version_info.json gets that version so
    the client re-downloads the table.
Versions come from data/overrides.json.

    python tools/build_overrides.py
"""
from __future__ import annotations

import base64
import json
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mst  # noqa: E402

ROOT = mst.ROOT
DATA = ROOT / "data"
OVERRIDES = mst.DEPLOY / "overrides"
SGTEXT_DIR = mst.DEPLOY / "game_content" / "content" / "sgtext"


def build_sgtext(base: int, version: int) -> str:
    src = SGTEXT_DIR / f"sgtext_en_{base}.zip"
    with zipfile.ZipFile(src) as z:
        name = z.namelist()[0]
        csv = base64.b64decode(z.read(name)).decode("utf-8")
    lines = csv.split("\n")
    header, rows = lines[0], [l for l in lines[1:] if l]
    cols = header.count("^")  # 6 language columns
    patch = {}
    for line in (DATA / "sgtext_en_patch.csv").read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        key, _, text = line.partition("^")
        patch[key] = key + ("^" + text) * cols
    out = []
    for row in rows:
        key = row.split("^", 1)[0]
        out.append(patch.pop(key, row))
    out.extend(patch.values())
    new_csv = "\n".join([header] + out) + "\n"
    dst = OVERRIDES / "content" / "sgtext" / f"sgtext_en_{version}.zip"
    dst.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr(f"sgtext_en_{version}.csv", base64.encodebytes(new_csv.encode("utf-8")))
    return f"/sgtext/sgtext_en_{version}.zip"


def build_resource(base_version: int, new_version: int, replacements: dict[str, str]) -> None:
    rows = mst.decode_table("F_RESOURCE_MST", base_version)  # the archive's table, not the bumped one
    done = set()
    for r in rows:
        p = r.get("ZwstRU92")
        if p in replacements:
            r["ZwstRU92"] = replacements[p]
            done.add(p)
    missing = set(replacements) - done
    if missing:
        sys.exit(f"F_RESOURCE_MST has no row for {missing}")
    files = mst.encode_table("F_RESOURCE_MST", rows, new_version, OVERRIDES / "mst")
    for f in files:
        print(f"  {f.relative_to(ROOT)} ({f.stat().st_size} bytes)")
    # version_info.json: the client re-downloads the table when the version changes
    doc = json.loads(mst.VERSION_INFO.read_text(encoding="utf-8"))
    row = next(r for r in doc[next(iter(doc))] if r["moWQ30GH"] == "F_RESOURCE_MST")
    if row["d2RFtP8T"] != str(new_version):
        row["d2RFtP8T"] = str(new_version)
        mst.VERSION_INFO.write_text(json.dumps(doc, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print(f"  version_info.json: F_RESOURCE_MST -> {new_version} (run `dev.py reload`)")


def main():
    cfg = json.loads((DATA / "overrides.json").read_text(encoding="utf-8"))
    sg = cfg["sgtext_en"]
    new_path = build_sgtext(sg["base"], sg["version"])
    print(f"  {new_path} built from sgtext_en_{sg['base']} + data/sgtext_en_patch.csv")
    res = cfg["F_RESOURCE_MST"]
    build_resource(res["base"], res["version"], {f"/sgtext/sgtext_en_{sg['base']}.zip": new_path})
    if "phase" in cfg:
        import build_phase
        build_phase.build(cfg["phase"]["cutoff"], cfg["phase"]["bump"])


if __name__ == "__main__":
    main()
