#!/usr/bin/env python3
"""
build_phase.py - phase-filtered copies of the downloaded MST tables (CLAUDE.md "Phasing design",
lever 2). A phase is a cutoff month on data/*_timeline.json (first month each unit / mission was
seen in Global): everything first seen at or before the cutoff is in.

What is filtered (served from deploy/overrides/mst/ with a bumped version, see tools/mst.py):
  F_UNIT_MST          units first seen <= cutoff, plus the units their kept evolutions consume
                      (evolution materials the datamine only lists later);
  F_UNIT_EVO_MST      evolutions whose source AND target are kept (a later 6*/7* form appears
                      when its phase comes), F_UNIT_COMMENT_MST by unit;
  F_MISSION_MST       missions first seen <= cutoff; F_DUNGEON_MST / F_AREA_MST / F_LAND_MST keep
                      the dungeons / areas / lands that still hold a mission; F_MISSION_SCRIPT_MST
                      and F_MISSION_EP3_MST by mission.
Everything else (skills, items, resources...) is served unchanged: the client only reaches those
rows through units and missions. version_info.json gets the new versions and row counts (the
client re-downloads a table whose version changed and checks the row count). The kept id sets go
to server/deploy/phase.json for server-side enforcement.

The timelines are keyed by id where the datamine snapshot was, by name before (Aug 2014); a named
entry is matched to the id-keyed entry of the same name (and land / area for missions).

    python tools/build_phase.py            # uses data/overrides.json "phase"
"""
from __future__ import annotations

import collections
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mst  # noqa: E402

ROOT = mst.ROOT
DATA = ROOT / "data"
OUT_DIR = mst.DEPLOY / "overrides" / "mst"

# raw keys (the served tables use obfuscated keys; readable names from mst-decoded.zip's 3-mst-translated)
K = {
    "unit_id": "pn16CNah",
    "mission_id": "j28VNcUW", "land_id": "9C64Qwe0", "area_id": "VjCY7rX4", "dungeon_id": "MHx05sXt",
    "evo_from": "pn16CNah", "evo_to": "74VFwuTd",
    "area_parent": "sHMai2ZG",  # F_AREA_MST: the folder area a Vortex area is listed under (AreaMstList::getActiveList)
}
# F_UNIT_EVO_MST material columns evoMaterialType1..10 (1 = unit, 2 = item) / evoMaterialId1..10
EVO_MAT_TYPES = ["Xyt6rhx2", "0tna4Idu", "6GwnugW3", "hdF8ND2H", "nB7pFdR0", "IZUvR489", "bNRUuatB", "2BFgYLjg", "18Oz7z8k", "EK7jR4rB"]
EVO_MAT_IDS = ["85X6JHQA", "wh3YRU08", "7MxucW2J", "j7fTS3ca", "Hb8yfmv7", "Voht18AP", "3g8brFoq", "agp4CKEV", "eKPWNoLn", "MVidsUNV"]


def ints(s) -> list[int]:
    return [int(x) for x in str(s or "").split(",") if x.strip().lstrip("-").isdigit()]


def phase_sets(cutoff: str) -> tuple[set[int], set[int]]:
    """Unit and mission ids first seen at or before `cutoff` (YYYY-MM)."""
    units = json.loads((DATA / "unit_timeline.json").read_text(encoding="utf-8"))
    missions = json.loads((DATA / "mission_timeline.json").read_text(encoding="utf-8"))

    by_name = collections.defaultdict(list)
    for k, v in units.items():
        if k.isdigit() and v.get("name"):
            by_name[v["name"]].append(int(k))
    unit_ids = {int(k) for k, v in units.items() if k.isdigit() and v["first_seen"] <= cutoff}
    unmatched = []
    for k, v in units.items():
        if not k.isdigit() and v["first_seen"] <= cutoff:
            if k in by_name:
                unit_ids.update(by_name[k])
            else:
                unmatched.append(k)
    if unmatched:
        print(f"  units named in the timeline without an id: {unmatched}")

    m_by_name = collections.defaultdict(list)
    for k, v in missions.items():
        if k.isdigit() and v.get("name"):
            m_by_name[(v["name"], v.get("land"), v.get("area"))].append(int(k))
    mission_ids = {int(k) for k, v in missions.items() if k.isdigit() and v["first_seen"] <= cutoff}
    unmatched = []
    for k, v in missions.items():
        if not k.isdigit() and v["first_seen"] <= cutoff:
            key = (k, v.get("land"), v.get("area"))
            if key in m_by_name:
                mission_ids.update(m_by_name[key])
            else:
                unmatched.append(key)
    if unmatched:
        print(f"  missions named in the timeline without an id: {unmatched}")
    return unit_ids, mission_ids


def build(cutoff: str, bump: int, write_versions: bool = True) -> dict:
    unit_ids, mission_ids = phase_sets(cutoff)
    tables = {name: mst.decode_table(name, mst.archive_version(name)) for name in (
        "F_UNIT_MST", "F_UNIT_EVO_MST", "F_UNIT_COMMENT_MST",
        "F_MISSION_MST", "F_DUNGEON_MST", "F_AREA_MST", "F_LAND_MST", "F_MISSION_SCRIPT_MST", "F_MISSION_EP3_MST")}

    all_units = {int(r[K["unit_id"]]) for r in tables["F_UNIT_MST"]}
    unit_ids &= all_units
    # evolutions: keep those whose target is in the phase; their unit materials come along
    evo_kept = [r for r in tables["F_UNIT_EVO_MST"] if int(r[K["evo_from"]]) in unit_ids and int(r[K["evo_to"]]) in unit_ids]
    added = set()
    for r in evo_kept:
        for t_key, id_key in zip(EVO_MAT_TYPES, EVO_MAT_IDS):
            mid = ints(r.get(id_key))
            if str(r.get(t_key)) == "1" and mid and mid[0] in all_units and mid[0] not in unit_ids:
                added.add(mid[0])
    unit_ids |= added
    evo_kept = [r for r in tables["F_UNIT_EVO_MST"] if int(r[K["evo_from"]]) in unit_ids and int(r[K["evo_to"]]) in unit_ids]

    all_missions = {int(r[K["mission_id"]]) for r in tables["F_MISSION_MST"]}
    mission_ids &= all_missions
    mis_kept = [r for r in tables["F_MISSION_MST"] if int(r[K["mission_id"]]) in mission_ids]
    dungeons = {int(r[K["dungeon_id"]]) for r in mis_kept}
    areas = {int(r[K["area_id"]]) for r in mis_kept}
    lands = {int(r[K["land_id"]]) for r in mis_kept}

    out = {
        "F_UNIT_MST": [r for r in tables["F_UNIT_MST"] if int(r[K["unit_id"]]) in unit_ids],
        "F_UNIT_EVO_MST": evo_kept,
        "F_UNIT_COMMENT_MST": [r for r in tables["F_UNIT_COMMENT_MST"] if int(r[K["unit_id"]]) in unit_ids],
        "F_MISSION_MST": mis_kept,
        "F_DUNGEON_MST": [r for r in tables["F_DUNGEON_MST"] if int(r[K["dungeon_id"]]) in dungeons],
        "F_AREA_MST": [r for r in tables["F_AREA_MST"] if int(r[K["area_id"]]) in areas],
        "F_LAND_MST": [r for r in tables["F_LAND_MST"] if int(r[K["land_id"]]) in lands],
        "F_MISSION_SCRIPT_MST": [r for r in tables["F_MISSION_SCRIPT_MST"] if int(r[K["mission_id"]]) in mission_ids],
        "F_MISSION_EP3_MST": [r for r in tables["F_MISSION_EP3_MST"] if int(r[K["mission_id"]]) in mission_ids],
    }
    # the Vortex list shows folder areas; an area under one needs the folder permitted too
    parents = {str(r[K["area_id"]]): ints(r.get(K["area_parent"]))[0]
               for r in out["F_AREA_MST"] if int(r[K["land_id"]]) == 99 and ints(r.get(K["area_parent"])) and ints(r.get(K["area_parent"]))[0] > 0}
    # a folder area is served whenever one of its children is (the mission filter alone may drop it)
    all_areas = {int(r[K["area_id"]]): r for r in tables["F_AREA_MST"]}
    for parent in set(parents.values()):
        if parent not in areas and parent in all_areas:
            areas.add(parent)
            out["F_AREA_MST"].append(all_areas[parent])

    # unlock chains: a kept mission / dungeon must not require a dropped mission
    for name, key in (("F_MISSION_MST", "HSRhkf70"), ("F_DUNGEON_MST", "HSRhkf70"), ("F_AREA_MST", "HSRhkf70"), ("F_LAND_MST", "HSRhkf70")):
        broken = [r for r in out[name] if any(x not in mission_ids and x != 0 for x in ints(r.get(key)))]
        if broken:
            print(f"  warning: {len(broken)} {name} rows require a mission outside the phase")

    doc = json.loads(mst.VERSION_INFO.read_text(encoding="utf-8"))
    rows = doc[next(iter(doc))]
    for name, data in out.items():
        base = mst.archive_version(name)
        version = base + bump
        files = mst.encode_table(name, data, version, OUT_DIR)
        row = next(r for r in rows if r["moWQ30GH"] == name)
        row["d2RFtP8T"], row["5kbnkTp0"] = str(version), str(len(data))
        print(f"  {name}: {len(tables[name])} -> {len(data)} rows, version {base} -> {version} ({', '.join(f.name for f in files)})")
    if write_versions:
        mst.VERSION_INFO.write_text(json.dumps(doc, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    vortex: dict[str, list[int]] = {}
    for r in mis_kept:
        if int(r[K["land_id"]]) == 99:
            vortex.setdefault(str(r[K["dungeon_id"]]), []).append(int(r[K["mission_id"]]))
    phase = {"cutoff": cutoff, "units": sorted(unit_ids), "missions": sorted(mission_ids),
             "dungeons": sorted(dungeons), "areas": sorted(areas), "lands": sorted(lands),
             "dungeon_missions": dict(sorted(vortex.items(), key=lambda kv: int(kv[0]))),  # Vortex dungeon -> missions
             "area_parents": dict(sorted(parents.items(), key=lambda kv: int(kv[0])))}  # Vortex area -> folder area
    (mst.DEPLOY / "phase.json").write_text(json.dumps(phase) + "\n", encoding="utf-8")
    print(f"  phase {cutoff}: {len(unit_ids)} units ({len(added)} evolution materials added), {len(mission_ids)} missions, "
          f"{len(dungeons)} dungeons, {len(areas)} areas, lands {sorted(lands)} -> deploy/phase.json")
    return phase


def main():
    cfg = json.loads((DATA / "overrides.json").read_text(encoding="utf-8"))["phase"]
    build(cfg["cutoff"], cfg["bump"])


if __name__ == "__main__":
    main()
