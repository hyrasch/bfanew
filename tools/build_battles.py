#!/usr/bin/env python3
"""Build server/deploy/battle/<mission_id>.json (the MissionStart battle data) from the
community datamine (cheahjs/bravefrontier_data) and the decoded 2022 unit table.

The datamine's missions_parsed/<Mission name>_<YYYY-DD-MM-HH-MM>.json files are 2014 captures of
the live server's mission data, already parsed: per battle the enemies with stats, element,
AI id, act counts, drops. They lack what the server generated per row (monster ids, field
positions, sprite file names), so this script:
  - takes the newest capture of each mission name and maps the name to the F_MISSION_MST id(s)
    through the datamine's missions.json;
  - numbers the battles / monsters itself (ids <mission><battle><slot>);
  - lays the enemies out on the 640x440 battle field (the client scales these by 0.5);
  - fills the animation fields from the unit the enemy is based on (F_UNIT_MST, mst-decoded.zip:
    effect/damage frames, move types, HP bar offset), since those enemies use the unit's sprites;
  - rebuilds the AI rows from ai.json in the client's raw format;
  - gives every enemy its captured skills: the capture names them (with hit count, damage
    distribution and parsed effects) and the client's own F_SKILL_MST holds them, so each is
    matched to a skill id + level and written as MonsterMst F4bQ7r8C "id:lv@id:lv"
    (BattleUnit::setUnitSkill(MonsterMst*) parses that and reads UnitSkillMstList), and the
    AI's "set skill" conditions become the client's "skill:<index>" terms.

The output keys are the wire keys of MissionBattleData (packet-generator/assets/net/mission_start.kdl),
so the handler can send the lists untouched.

Usage:
  python tools/build_battles.py --datamine ..\\tools-dl\\bfdata\\bravefrontier_data ^
      --mst ..\\assets\\mst-decoded.zip --out server\\deploy\\battle [--only 10 11 12]
"""
import argparse
import base64
import glob
import json
import os
import re
import sys
import zipfile

ELEMENTS = {"fire": 1, "water": 2, "earth": 3, "thunder": 4, "light": 5, "dark": 6}

# enemy positions (x:y) on the 640x440 field, by number of enemies in the battle.
# The enemies stand on the LEFT, staggered top to bottom, the squad on the right (like the
# original game). Calibrated on BlueStacks: the x value lands where written, the y value shows
# about 165 units lower than written, so y 300 is ~30% up the field and y 400 ~53%.
LAYOUTS = {
    1: [(170, 390)],
    2: [(150, 435), (205, 340)],
    3: [(130, 445), (205, 395), (140, 340)],
    4: [(120, 455), (210, 415), (130, 365), (220, 320)],
    5: [(110, 460), (200, 425), (120, 385), (210, 345), (130, 305)],
    6: [(100, 465), (190, 435), (110, 395), (200, 360), (120, 325), (210, 290)],
}


def layout(n):
    if n in LAYOUTS:
        return LAYOUTS[n]
    step = 180 / max(1, n - 1)
    return [(100 if i % 2 == 0 else 200, int(470 - i * step)) for i in range(n)]


def capture_date(fname):
    m = re.search(r"_(\d{4})-(\d{1,2})-(\d{1,2})-(\d{1,2})-(\d{1,2})\.json$", fname)
    if not m:
        return (0, 0, 0, 0, 0)
    y, d, mo, h, mi = (int(x) for x in m.groups())  # the files are named YYYY-DD-MM-HH-MM
    return (y, mo, d, h, mi)


def load_units(mst_zip):
    units = {}
    with zipfile.ZipFile(mst_zip) as z:
        for n in z.namelist():
            if n.startswith("3-mst-translated/F_UNIT_MST"):
                for u in json.loads(z.read(n)):
                    units[u["unitId"]] = u
    return units


def load_skills(mst_zip):
    """skill id -> {"frames": damageFrames, "procs": skillTypeId, "levels": [(lv, params)]} from F_SKILL_MST
    and F_SKILL_LEVEL_MST (activeSkillInfoByLevel is "lv:cost:params@params...")."""
    skills = {}
    with zipfile.ZipFile(mst_zip) as z:
        for n in z.namelist():
            if n.startswith("3-mst-translated/F_SKILL_MST"):
                for r in json.loads(z.read(n)):
                    skills[r["bbId"]] = {"frames": r.get("damageFrames", ""), "procs": r.get("skillTypeId", ""), "levels": []}
        for n in z.namelist():
            if n.startswith("3-mst-translated/F_SKILL_LEVEL_MST"):
                for r in json.loads(z.read(n)):
                    sk = skills.get(r["bbId"])
                    if sk is None:
                        continue
                    f = (r.get("activeSkillInfoByLevel") or "").split(":", 2)
                    if len(f) == 3:
                        sk["levels"].append((f[0], f[2]))
    return skills


def load_monster_name_ids(sgtext_dir):
    """The monster ids the client's text table names (sgtext_monsters_<v>.zip, MST_MONSTERS_<id>_NAME):
    MonsterMst::getMonsterName shows that text when the monster id has one, the row's own name
    otherwise, so our generated ids must stay clear of them."""
    files = sorted(glob.glob(os.path.join(sgtext_dir, "sgtext_monsters_*.zip")),
                   key=lambda f: int(re.search(r"_(\d+)\.zip$", f).group(1)))
    ids = set()
    if not files:
        return ids
    with zipfile.ZipFile(files[-1]) as z:
        csv = base64.b64decode(z.read(z.namelist()[0])).decode("utf-8")
    for line in csv.split("\n")[1:]:
        m = re.match(r"MST_MONSTERS_(\d+)_NAME\^", line)
        if m:
            ids.add(m.group(1))
    return ids


def load_skill_names(sgtext_dir):
    """English skill name -> [skill ids] from the newest content/sgtext/sgtext_skillName_<v>.zip."""
    files = sorted(glob.glob(os.path.join(sgtext_dir, "sgtext_skillName_*.zip")),
                   key=lambda f: int(re.search(r"_(\d+)\.zip$", f).group(1)))
    names = {}
    if not files:
        return names
    with zipfile.ZipFile(files[-1]) as z:
        csv = base64.b64decode(z.read(z.namelist()[0])).decode("utf-8")
    for line in csv.split("\n")[1:]:
        key, _, rest = line.partition("^")
        m = re.match(r"MST_SKILLS_(\d+)_NAME$", key)
        if m and rest:
            names.setdefault(rest.split("^")[0], []).append(m.group(1))
    return names


# proc ids whose parameters start with "bb atk%, bb flat atk, ..., bb dmg%" (bfdb skills.py)
ATTACK_PROCS = {"1": (0, 1, 5), "14": (0, 1, 5)}


def resolve_skill(cap, skills, names, own_bb=None, unit_bbs=()):
    """The (skill id, level) of the client's skill table that matches a captured enemy skill:
    same name, then the enemy unit's own brave burst, then a brave burst of a unit the client
    has (the ids the client is sure to hold), then the best of hit count, damage distribution
    and attack parameters."""
    cands = [i for i in names.get(cap.get("name"), []) if i in skills]
    if not cands:
        return None
    hits = cap.get("hits")
    dist = cap.get("hit dmg% distribution")
    effects = {}
    for e in (cap.get("levels") or {}).get("effects", []) or []:
        effects.update(e)
    best = None
    for sid in cands:
        sk = skills[sid]
        entries = [e for e in sk["frames"].split("@")[0].split(",") if e]
        score = 0
        if own_bb is not None and sid == str(own_bb):
            score += 16
        elif sid in unit_bbs:
            score += 8
        if hits is not None and len(entries) == hits:
            score += 4
        try:
            if dist is not None and [int(e.split(":")[1]) for e in entries] == list(dist):
                score += 2
        except (ValueError, IndexError):
            pass
        procs = sk["procs"].split("@")
        best_lv, best_lv_score = "1", -1
        for lv, params in sk["levels"] or [("1", "")]:
            lv_score = 0
            for proc, seg in zip(procs, params.split("@")):
                if proc in ATTACK_PROCS:
                    f = seg.split(",")
                    for idx, key in zip(ATTACK_PROCS[proc], ("bb atk%", "bb flat atk", "bb dmg%")):
                        try:
                            v = int(f[idx])
                        except (ValueError, IndexError):
                            continue
                        if effects.get(key, 0) == v:
                            lv_score += 1
            if lv_score > best_lv_score:
                best_lv, best_lv_score = lv, lv_score
        score += best_lv_score
        if best is None or score > best[0] or (score == best[0] and int(sid) < int(best[1])):
            best = (score, sid, best_lv)
    return best[1], best[2]


def load_mission_places(mst_zip):
    """mission id -> (dungeon, area, land) from F_MISSION_MST"""
    places = {}
    with zipfile.ZipFile(mst_zip) as z:
        for n in z.namelist():
            if n.startswith("3-mst-translated/F_MISSION_MST"):
                for m in json.loads(z.read(n)):
                    places[m["missionId"]] = (m.get("missionDungeonId", "0"), m.get("missionAreaId", "0"), m.get("missionLandId", "0"))
    return places


def load_mission_rewards(mst_zip):
    """mission id -> first-clear rewards from F_MISSION_MST missionRewards, a list of
    "type:id:count" (type 8 = gems, 2 = zel, 3 = karma, 1 = unit, 4 = item; the MST carries
    "type:id:count:x:y" entries separated by commas)."""
    rewards = {}
    with zipfile.ZipFile(mst_zip) as z:
        for n in z.namelist():
            if n.startswith("3-mst-translated/F_MISSION_MST"):
                for m in json.loads(z.read(n)):
                    out = []
                    for e in (m.get("missionRewards") or "").split(","):
                        f = e.split(":")
                        if len(f) >= 3 and f[0]:
                            out.append(f"{f[0]}:{f[1]}:{f[2]}")
                    rewards[m["missionId"]] = out
    return rewards


def ai_rows(ai_id, ai_json, fallback, skill_index=None, warn=None):
    """AIMst rows (wire keys) for one ai id: the capture's own 2014 AI when it has one (fallback),
    else the datamine's 2021 ai.json. skill_index maps a skill name to its 0-based position in the
    monster's skill list: the captures render the client's 1-based "skill:<n>" condition as
    "set skill": <name> (checked against ai.json, where the same AIs say skill:1 for the first)."""
    src = fallback or ai_json.get(str(ai_id)) or {}
    rows = []
    for i, a in enumerate(src.get("actions", [])):
        party = []
        for c in a.get("party conditions/set parameters", []):
            party.append(f"{c.get('target type', 0)}:{c.get('target param', 0)}:{c.get('type', 0)}:{c.get('parameters', 0)}")
        act = []
        for c in a.get("conditions/set parameters", []):
            ctype, cparam = c.get("type", "non"), c.get("parameters", 0)
            if ctype == "set skill" and str(cparam).isdigit():
                ctype = "skill"  # already the client's 1-based number
            elif ctype == "set skill":
                idx = (skill_index or {}).get(cparam)
                if idx is None:
                    if warn:
                        warn(f"ai {ai_id}: skill {cparam!r} is not in the enemy's skill list, condition dropped")
                    continue
                ctype, cparam = "skill", idx + 1
            act.append(f"{ctype}:{cparam}")
        term = ("@".join(party) if party else "0:0:0:0") + "#" + ("@".join(act) if act else "non:0")
        action = a.get("action") or {}
        rows.append({
            "4eEVw5hL": str(ai_id),
            "yu18xScw": str(a.get("priority", i + 1)),
            "q7Nit8JW": term,
            "4xctV8gF": str(a.get("target type", 2)),
            "VBj9u0ot": str(a.get("target conditions", "random")),
            "Hhgi79M1": f"{action.get('type', 'attack')}@{action.get('parameters', '-1')}",
            "ug9xV4Fz": str(a.get("percent", 100.0)),
            "L8PCsu0K": str(src.get("name", "")),
        })
    if not rows:
        rows.append({
            "4eEVw5hL": str(ai_id), "yu18xScw": "1", "q7Nit8JW": "0:0:0:0#non:0", "4xctV8gF": "2",
            "VBj9u0ot": "random", "Hhgi79M1": "attack@-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,",
            "ug9xV4Fz": "100.0", "L8PCsu0K": "attack random",
        })
    return rows


def build(mission_id, info, battles, units, items_by_name, ai_json, warn, unlocks=(), first_clear=(), skills=None, skill_names=None, skill_stats=None, unit_bbs=(), named_ids=(), strict_bbs=False):
    mid = str(mission_id)
    out = {
        "energy_use": int(info.get("energy_use", 0)),
        "battle_count": len(battles),
        "exp": int(info.get("xp", 0)),
        "zel": int(info.get("zel", 0)),
        "karma": int(info.get("karma", 0)),
        "unlocks": list(unlocks),
        "first_clear": list(first_clear),  # "type:id:count": 8 gems, 3 zel, 11 karma, 6 unit, 7 sphere, 4 item (see MissionEnd.cpp)
        "pj41dy9g": [], "75t0sx9z": [], "n64NOpfy": [], "U0v5IeJo": [], "89ausgc4": [], "m9Ada1PN": [],
    }
    ai_ids = {}
    ai_skill_index = {}
    for b, battle in enumerate(battles, start=1):
        gid = f"{mid}{b:02d}"
        monsters = battle.get("monsters", [])
        out["pj41dy9g"].append({
            "ZSf8e1MG": gid, "Qzhp8B40": gid, "j28VNcUW": mid, "VETu07N6": str(b),
            "etM5TCb9": "1" if b == len(battles) else "0", "5aetPz3C": "0", "mFxqsc7Y": "0",
        })
        pos = layout(len(monsters))
        for i, m in enumerate(monsters, start=1):
            monster_id = f"{gid}{i}"
            while monster_id in named_ids:  # a 2022 monster name would replace ours (see load_monster_name_ids)
                monster_id = "9" + monster_id
            unit_id = str(m.get("unit id", ""))
            u = units.get(unit_id)
            if u is None:
                warn(f"mission {mid} battle {b}: unit {unit_id} ({m.get('_name')}) not in F_UNIT_MST")
                u = {}
            stats = m.get("stats", {})
            acts = str(m.get("act counts", "1~1")).split("~")
            try:
                min_act, max_act = int(acts[0]), int(acts[-1])
            except ValueError:
                min_act, max_act = 1, 1
            drops = []
            for d in m.get("item drop", []) or []:
                item_id = items_by_name.get(d.get("_name"))
                if item_id is None:
                    warn(f"mission {mid}: unknown item {d.get('_name')!r}")
                    continue
                drops.append(f"{d.get('percent', 0)}:{item_id}:{d.get('drop rest?', 0)}")
            resists = m.get("condition resists", {}) or {}
            res = ":".join(str(int(resists.get(k, 0))) for k in ("poison%", "weaken%", "sick%", "injury%", "curse%", "paralysis%"))
            ai_id = str(m.get("ai id", 1))
            ai_ids.setdefault(ai_id, m.get("ai"))
            # the enemy's skills: "id:lv@id:lv" of the client's own skill table
            skill_ids, skill_index = [], {}
            for cap in m.get("skills") or []:
                hit = resolve_skill(cap, skills or {}, skill_names or {}, u.get("bbId") if u else None, unit_bbs)
                if hit and strict_bbs and hit[0] not in unit_bbs:
                    # the client's UnitSkillMstList holds the brave bursts of the units it has, not every
                    # F_SKILL_MST row: an id outside that set is a null lookup at mission start (SIGSEGV)
                    warn(f"skill {cap.get('name')!r} of {m.get('_name')} (mission {mid}): {hit[0]} is not a brave burst of a phase unit, dropped")
                    hit = None
                if skill_stats is not None:
                    skill_stats["resolved" if hit else "unresolved"] += 1
                if not hit:
                    if not strict_bbs:
                        warn(f"skill {cap.get('name')!r} of {m.get('_name')} (mission {mid}): no skill of that name in the client table")
                    continue
                skill_index.setdefault(cap.get("name"), len(skill_ids))
                skill_ids.append(f"{hit[0]}:{hit[1]}")
            ai_skill_index.setdefault(ai_id, skill_index)
            x, y = pos[i - 1]
            out["75t0sx9z"].append({
                "Qzhp8B40": gid, "o49dYfpH": monster_id, "hZtF1s8B": str(i), "3g8PW6x0": f"{x}:{y}",
                "i30R8TAs": ",".join(drops), "hw3L0uVj": "", "5wB9SHAV": "",
            })
            out["U0v5IeJo"].append({
                "o49dYfpH": monster_id,
                "3evIn0zZ": str(m.get("_name", "")),
                "e7DK0FQT": str(stats.get("hp", 1)),
                "67CApcti": str(stats.get("atk", 1)),
                "q08xLEsy": str(stats.get("def", 0)),
                "iNy0ZU5M": str(ELEMENTS.get(m.get("element"), 1)),
                "eyUo6a8c": u.get("effectFrames", "24:121:1"),
                "6Aou5M9r": u.get("damageFrames", "30:100:2:1"),
                "n9h7p02P": u.get("dropCheckCount", "2"),
                "d96tuT2E": "0",
                "Rs7bCE3t": "0",
                "2EF0d6ue": "",
                "QqfI9mM4": "",
                "pn16CNah": unit_id,
                "qp37xTDh": str(m.get("desc", "")),
                "J2hPXGo5": u.get("moveSpeedType", "2"),
                "mv4o39Uz": u.get("attackMoveType", "1"),
                "h2L1YI90": u.get("unitDisplayMoveType_h2L1YI90", "1"),
                "i74vGUFa": ai_id,
                "D4Y5bWK7": u.get("unitDisplayAfterImage_D4Y5bWK7", "1"),
                "oMGC3hW0": str(max_act),
                "m4EK7Gt6": str(min_act),
                "6fwL59FT": "100",
                "F4bQ7r8C": "@".join(skill_ids),
                "XE7Yi5c3": u.get("skillMoveType", "1"),
                "CEeqs63b": res,
                "Najhr8m6": str(m.get("zel max drop", 0)),
                "9FN0GAei": str(m.get("zel drop count", 0)),
                "HTVh8a65": str(m.get("karma max drop", 0)),
                "vNk2sI4X": str(m.get("karma drop count", 0)),
                "3BpHN6VD": u.get("unitDisplayHpPosition_3BpHN6VD", "0,0"),
                "hjAy9St3": "",
                "2Smu5Mtq": "",
                "CYk84E3W": u.get("unitDisplayMoveOffset_jFX0a7qe", "0,0"),
                "Y6bd4fXp": "",
                "jm6JSK2D": "",
                "9pXSKmn1": "0",
                "Lkh6gYkT": "",
            })
    for ai_id, fallback in ai_ids.items():
        out["89ausgc4"].extend(ai_rows(ai_id, ai_json, fallback, ai_skill_index.get(ai_id), warn))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--datamine", required=True, help="path to a bravefrontier_data checkout (missions.json, missions_parsed/, items.json, ai.json)")
    ap.add_argument("--mst", required=True, help="path to mst-decoded.zip (for F_UNIT_MST)")
    ap.add_argument("--out", required=True, help="output folder (server/deploy/battle)")
    ap.add_argument("--only", nargs="*", help="mission ids to build (default: every captured mission)")
    ap.add_argument("--sgtext", help="content/sgtext folder holding sgtext_skillName_<v>.zip (default: <out>/../game_content/content/sgtext)")
    ap.add_argument("--phase", help="server/deploy/phase.json: enemy skills are then limited to the brave bursts of the phase's units (what the client's UnitSkillMstList holds)")
    a = ap.parse_args()

    dm = a.datamine
    missions = json.load(open(os.path.join(dm, "missions.json"), encoding="utf-8"))
    items = json.load(open(os.path.join(dm, "items.json"), encoding="utf-8"))
    ai_json = json.load(open(os.path.join(dm, "ai.json"), encoding="utf-8"))
    units = load_units(a.mst)
    skills = load_skills(a.mst)
    sgtext_dir = a.sgtext or os.path.join(a.out, "..", "game_content", "content", "sgtext")
    skill_names = load_skill_names(sgtext_dir)
    named_ids = load_monster_name_ids(sgtext_dir)
    skill_stats = {"resolved": 0, "unresolved": 0}
    unit_bbs = {str(u.get("bbId")) for u in units.values()}  # brave bursts of units the client knows
    strict = False
    if a.phase:
        phase_units = {str(u) for u in json.load(open(a.phase, encoding="utf-8")).get("units", [])}
        unit_bbs = {str(u.get("bbId")) for uid, u in units.items() if uid in phase_units}
        strict = True
    places = load_mission_places(a.mst)
    rewards = load_mission_rewards(a.mst)
    unlocked_by = {}
    for mid, info in missions.items():
        for req in str(info.get("requires", "0")).split(","):
            if req and req != "0":
                d, ar, l = places.get(mid, ("0", "0", "0"))
                unlocked_by.setdefault(req, []).append({"mission_id": mid, "dungeon_id": d, "area_id": ar, "land_id": l})
    items_by_name = {}
    for it in items.values():
        items_by_name.setdefault(it.get("name"), it.get("id"))

    by_name = {}
    for mid, info in missions.items():
        by_name.setdefault(info.get("name"), []).append((mid, info))

    captures = {}
    for f in glob.glob(os.path.join(dm, "missions_parsed", "*.json")):
        name = re.sub(r"_\d{4}-\d{1,2}-\d{1,2}-\d{1,2}-\d{1,2}\.json$", "", os.path.basename(f))
        if name not in captures or capture_date(f) > capture_date(captures[name]):
            captures[name] = f

    warnings = []
    os.makedirs(a.out, exist_ok=True)
    written = 0
    only = set(a.only or [])
    for name, f in sorted(captures.items()):
        targets = by_name.get(name)
        if not targets:
            warnings.append(f"capture {name!r}: no mission of that name in missions.json")
            continue
        if len(targets) > 1:
            warnings.append(f"capture {name!r}: {len(targets)} missions share the name ({', '.join(t[0] for t in targets)}), all get this data")
        data = json.load(open(f, encoding="utf-8"))
        battles = (data.get(name) or next(iter(data.values()))).get("battles", [])
        for mid, info in targets:
            if only and mid not in only:
                continue
            out = build(mid, info, battles, units, items_by_name, ai_json, warnings.append, unlocked_by.get(mid, ()), rewards.get(mid, ()), skills, skill_names, skill_stats, unit_bbs, named_ids, strict)
            with open(os.path.join(a.out, f"{mid}.json"), "w", encoding="utf-8", newline="\n") as fo:
                json.dump(out, fo, ensure_ascii=False, indent=1)
            written += 1
    # missions without a capture: borrow the battles of the nearest captured mission of the
    # same dungeon (same enemies, the mission's own energy / reward / unlocks), flagged in the file
    captured_ids = {}
    for name, f in captures.items():
        for mid, info in by_name.get(name, []):
            captured_ids[mid] = f
    by_dungeon = {}
    for mid, info in missions.items():
        by_dungeon.setdefault((info.get("land"), info.get("area"), info.get("dungeon")), []).append(mid)
    borrowed = 0
    for mid, info in missions.items():
        if mid in captured_ids or (only and mid not in only):
            continue
        siblings = [m for m in by_dungeon.get((info.get("land"), info.get("area"), info.get("dungeon")), []) if m in captured_ids]
        if not siblings:
            continue
        src = min(siblings, key=lambda m: abs(int(m) - int(mid)) if m.isdigit() and mid.isdigit() else 1 << 30)
        data = json.load(open(captured_ids[src], encoding="utf-8"))
        battles = next(iter(data.values())).get("battles", [])
        out = build(mid, info, battles, units, items_by_name, ai_json, warnings.append, unlocked_by.get(mid, ()), rewards.get(mid, ()), skills, skill_names, skill_stats, unit_bbs, named_ids, strict)
        out["borrowed_from"] = src
        with open(os.path.join(a.out, f"{mid}.json"), "w", encoding="utf-8", newline="\n") as fo:
            json.dump(out, fo, ensure_ascii=False, indent=1)
        borrowed += 1
    for w in sorted(set(warnings)):
        print("warning:", w)
    print(f"{written} mission files written to {a.out} from {len(captures)} captures, {borrowed} more borrowed from a captured mission of the same dungeon; "
          f"enemy skills: {skill_stats['resolved']} matched to the client's skill table, {skill_stats['unresolved']} not")


if __name__ == "__main__":
    main()
