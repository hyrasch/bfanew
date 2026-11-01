#!/usr/bin/env python3
"""Synthesize server/deploy/battle/<mission_id>.json for the phase missions that have no capture
(and no captured sibling to borrow from): see build_battles.py for the captured ones.

Sources, best first:
  - data/wiki/*.txt: the Brave Frontier Global wiki's zone pages (one page per dungeon, saved as
    the raw wikitext: open https://bravefrontierglobal.fandom.com/wiki/<Zone_Name>?action=raw in
    a browser and save the text; several pages may share one file under "== Zone Name ==" lines).
    Each {{Quest}} gives the quest's bosses (boss1..6 = unit ids, bossNhp), the capturable units
    and the energy / battle count; {{Zone:Footer}} lists the zone's mobs and drops by name.
  - the captures of the same land (missions_parsed/): their mobs and bosses make the pool of a
    dungeon without a wiki page.
The enemies' stats come from the unit table (units.json max stats) scaled by factors fitted on the
captures (log-linear in the mission's XP, separate for mobs and for the last battle's bosses);
the AI / act counts / resists of a unit come from its captures when it was ever captured, else
from the most common mob or boss AI of the captures. The files are marked "synthesized".

Usage:
  python tools/synth_battles.py --datamine ..\\tools-dl\\bfdata\\bravefrontier_data --mst ..\\assets\\mst-decoded.zip
      --out server\\deploy\\battle --phase server\\deploy\\phase.json [--wiki data\\wiki] [--only 210 211] [--force]
"""
import argparse
import collections
import glob
import json
import math
import os
import random
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_battles as bb  # noqa: E402
import build_pools  # noqa: E402

ELEMENT_NAMES = {1: "fire", 2: "water", 3: "earth", 4: "thunder", 5: "light", 6: "dark"}


# ------------------------------------------------------------------ wiki pages
def parse_templates(text):
    """{{Name |k = v |k2 = v2}} blocks -> [(name, {k: v})], tolerant of newlines inside values."""
    out = []
    for m in re.finditer(r"\{\{\s*(?:Template:)?([A-Za-z:]+)(.*?)\}\}", text, re.S):
        name = m.group(1)
        params = {}
        for part in re.split(r"\n?\|", m.group(2)):
            if "=" not in part:
                continue
            k, v = part.split("=", 1)
            params[k.strip()] = v.strip()
        out.append((name, params))
    return out


def load_wiki(folder):
    """zone name -> {"element": str, "quests": {quest name: params}, "units": [names], "drops": [names]}"""
    zones = {}
    for f in glob.glob(os.path.join(folder, "*.txt")):
        text = open(f, encoding="utf-8").read()
        default = os.path.splitext(os.path.basename(f))[0]
        sections = re.split(r"^== (.+?) ==\s*$", text, flags=re.M)
        pages = [(default, sections[0])] if len(sections) == 1 else [(sections[i], sections[i + 1]) for i in range(1, len(sections), 2)]
        for title, body in pages:
            zone = {"element": "", "quests": {}, "units": [], "drops": [], "units_text": ""}
            for name, p in parse_templates(body):
                if name.endswith("Zone:Header"):
                    zone["element"] = p.get("element", "").strip()
                    title = p.get("name", title).strip() or title
                elif name == "Quest":
                    zone["quests"][p.get("quest", "").strip()] = p
                elif name.endswith("Zone:Footer"):
                    zone["units_text"] = p.get("units", "")
                    zone["units"] = re.findall(r"\[\[([^\]|]+)", p.get("units", ""))
                    zone["drops"] = [d.strip(" '") for d in re.findall(r"\[\[([^\]|]+)|([A-Z][A-Za-z' ]+)", p.get("drops", "")) for d in d if d]
            if zone["quests"] or zone["units"]:
                zones[title.strip()] = zone
    return zones


# ------------------------------------------------------------------ calibration on the captures
def fit_loglog(points):
    """least squares of log(y) = a + b log(x) -> (a, b); a flat median when there is no spread"""
    pts = [(math.log(x), math.log(y)) for x, y in points if x > 0 and y > 0]
    if len(pts) < 3:
        return (math.log(sum(y for _, y in points) / max(1, len(points))) if points else 0.0, 0.0)
    n = len(pts)
    mx = sum(x for x, _ in pts) / n
    my = sum(y for _, y in pts) / n
    sxx = sum((x - mx) ** 2 for x, _ in pts)
    if sxx < 1e-9:
        return (my, 0.0)
    b = sum((x - mx) * (y - my) for x, y in pts) / sxx
    return (my - b * mx, b)


def factor(fit, xp):
    a, b = fit
    return math.exp(a + b * math.log(max(5, xp)))


# ------------------------------------------------------------------ names -> unit ids
def resolve_name(name, phase_units, names_to_ids, captured_names):
    name = name.strip()
    if name in captured_names:
        return captured_names[name]
    ids = [i for i in names_to_ids.get(name, []) if i in phase_units]
    if ids:
        return min(ids, key=int)
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--datamine", required=True)
    ap.add_argument("--mst", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--phase", required=True, help="server/deploy/phase.json (the missions to cover)")
    ap.add_argument("--units", default=os.path.join("server", "deploy", "units.json"))
    ap.add_argument("--wiki", default=os.path.join("data", "wiki"))
    ap.add_argument("--only", nargs="*")
    ap.add_argument("--force", action="store_true", help="overwrite existing files (only synthesized ones are ever overwritten)")
    ap.add_argument("--sgtext")
    a = ap.parse_args()

    dm = a.datamine
    missions = json.load(open(os.path.join(dm, "missions.json"), encoding="utf-8"))
    items = json.load(open(os.path.join(dm, "items.json"), encoding="utf-8"))
    ai_json = json.load(open(os.path.join(dm, "ai.json"), encoding="utf-8"))
    units_mst = bb.load_units(a.mst)
    skills = bb.load_skills(a.mst)
    sgtext_dir = a.sgtext or os.path.join(a.out, "..", "game_content", "content", "sgtext")
    skill_names = bb.load_skill_names(sgtext_dir)
    named_ids = bb.load_monster_name_ids(sgtext_dir)
    phase = json.load(open(a.phase, encoding="utf-8"))
    phase_units = {str(u) for u in phase.get("units", [])}
    unit_bbs = {str(u.get("bbId")) for uid, u in units_mst.items() if uid in phase_units}  # what the client's UnitSkillMstList holds
    places = bb.load_mission_places(a.mst)
    rewards = bb.load_mission_rewards(a.mst)
    unlocked_by = {}
    for mid, info in missions.items():
        for req in str(info.get("requires", "0")).split(","):
            if req and req != "0":
                d, ar, l = places.get(mid, ("0", "0", "0"))
                unlocked_by.setdefault(req, []).append({"mission_id": mid, "dungeon_id": d, "area_id": ar, "land_id": l})
    items_by_name = {}
    for it in items.values():
        items_by_name.setdefault(it.get("name"), it.get("id"))
    units_srv = json.load(open(a.units, encoding="utf-8"))
    phase_missions = [str(m) for m in phase.get("missions", [])]
    english = build_pools.unit_names()  # unit id -> English name
    names_to_ids = collections.defaultdict(list)
    for uid, nm in english.items():
        names_to_ids[nm].append(str(uid))

    # the captures: calibration, the per-unit AI, the land pools
    by_name = {}
    for mid, info in missions.items():
        by_name.setdefault(info.get("name"), []).append((mid, info))
    captures = {}
    for f in glob.glob(os.path.join(dm, "missions_parsed", "*.json")):
        name = re.sub(r"_\d{4}-\d{1,2}-\d{1,2}-\d{1,2}-\d{1,2}\.json$", "", os.path.basename(f))
        if name not in captures or bb.capture_date(f) > bb.capture_date(captures[name]):
            captures[name] = f
    pts = {("mob", s): [] for s in ("hp", "atk", "def")}
    pts.update({("boss", s): [] for s in ("hp", "atk", "def")})
    unit_profile = {}        # unit id -> the monster dict of its latest capture (ai, acts, resists, passive)
    captured_names = {}      # monster name -> unit id
    land_mobs = collections.defaultdict(set)    # land -> unit ids seen outside last battles
    land_bosses = collections.defaultdict(set)  # land -> unit ids of last battles
    ai_count = {"mob": collections.Counter(), "boss": collections.Counter()}
    for name, f in captures.items():
        targets = by_name.get(name) or []
        data = json.load(open(f, encoding="utf-8"))
        battles = next(iter(data.values())).get("battles", [])
        xp = int(targets[0][1].get("xp", 0)) if targets else 0
        land = targets[0][1].get("land") if targets else None
        for b, battle in enumerate(battles, start=1):
            role = "boss" if b == len(battles) else "mob"
            for m in battle.get("monsters", []):
                uid = str(m.get("unit id", ""))
                if not uid:
                    continue
                unit_profile.setdefault(uid, m)
                if m.get("_name"):
                    captured_names.setdefault(m["_name"], uid)
                ai_count[role][str(m.get("ai id", ""))] += 1
                if land:
                    (land_bosses if role == "boss" else land_mobs)[land].add(uid)
                srv = units_srv.get(uid)
                if srv and xp > 0:
                    for s in ("hp", "atk", "def"):
                        mx = srv.get(s, [1, 1])[1]
                        v = (m.get("stats") or {}).get(s)
                        if mx and v:
                            pts[(role, s)].append((xp, v / mx))
    fits = {k: fit_loglog(v) for k, v in pts.items()}
    land_xp = {}
    for name, f in captures.items():
        for mid, info in by_name.get(name, []):
            if int(info.get("xp", 0)) > 0:
                land_xp.setdefault(info.get("land"), []).append(int(info.get("xp", 0)))
    land_xp = {l: sum(v) / len(v) for l, v in land_xp.items()}
    default_ai = {role: (c.most_common(1)[0][0] if c else "1") for role, c in ai_count.items()}
    print("calibration:", ", ".join(f"{r}.{s}: {math.exp(fits[(r, s)][0]):.2f}*xp^{fits[(r, s)][1]:.2f} ({len(pts[(r, s)])} pts)" for r, s in fits),
          "| default ai", default_ai)

    zones = load_wiki(a.wiki) if os.path.isdir(a.wiki) else {}
    print(f"{len(zones)} wiki zones: {', '.join(sorted(zones))}")
    # the land pools also take the wiki zones of the land
    land_of_zone = {}
    for mid, info in missions.items():
        land_of_zone.setdefault(info.get("dungeon"), info.get("land"))
    land_wiki_units = collections.defaultdict(set)
    for zname, z in zones.items():
        land = land_of_zone.get(zname)
        for n in z["units"]:
            uid = resolve_name(n, phase_units, names_to_ids, captured_names)
            if uid and land:
                land_wiki_units[land].add(uid)

    warnings = []
    os.makedirs(a.out, exist_ok=True)
    only = set(a.only or [])
    written = {"wiki": 0, "land": 0}
    skipped = 0
    for mid in phase_missions:
        if only and mid not in only:
            continue
        path = os.path.join(a.out, f"{mid}.json")
        if os.path.exists(path):
            try:
                existing = json.load(open(path, encoding="utf-8"))
            except Exception:
                existing = {}
            if not existing.get("synthesized") or not a.force:
                skipped += 1
                continue
        info = missions.get(mid)
        if not info:
            warnings.append(f"mission {mid}: not in missions.json")
            continue
        land, dungeon, qname = info.get("land"), info.get("dungeon"), info.get("name")
        xp = int(info.get("xp", 0))
        n = max(1, int(info.get("battle_count", 1)))
        rng = random.Random(int(mid))
        zone = zones.get(dungeon)
        quest = (zone or {}).get("quests", {}).get(qname)
        source = "wiki" if quest else "land"
        # the mob pool
        pool = []
        if zone:
            if re.search(r"\bAll\b", zone.get("units_text", "")):
                pool = sorted(land_mobs[land] | land_wiki_units[land])
            for nm in zone["units"]:
                uid = resolve_name(nm, phase_units, names_to_ids, captured_names)
                if uid:
                    pool.append(uid)
                else:
                    warnings.append(f"zone {dungeon!r}: unit {nm!r} unknown")
        if not pool:
            pool = sorted(land_mobs[land] | land_wiki_units[land])
        pool_land = land
        if not pool:
            # a land with no capture and no wiki page: the pool of the land whose captured missions
            # are nearest in XP (the enemies are scaled to this mission's XP anyway)
            cands = [l for l in land_xp if (land_mobs[l] | land_wiki_units[l])]
            if cands:
                pool_land = min(cands, key=lambda l: abs(math.log(max(5, land_xp[l])) - math.log(max(5, xp))))
                pool = sorted(land_mobs[pool_land] | land_wiki_units[pool_land])
        pool = [u for u in dict.fromkeys(pool) if u in units_srv and "Nymph" not in english.get(u, "")]
        if not pool:
            warnings.append(f"mission {mid} ({qname}): no mob pool for land {land!r}, skipped")
            continue
        if pool_land != land:
            source = f"land:{pool_land}"
        # the bosses
        bosses = []
        boss_hp = {}
        if quest:
            for i in range(1, 7):
                v = quest.get(f"boss{i}", "").strip()
                if v.isdigit():
                    bosses.append(v)
                    hp = quest.get(f"boss{i}hp", "").replace(",", "").strip()
                    if hp.isdigit():
                        boss_hp[len(bosses) - 1] = int(hp)
        if not bosses:
            cands = sorted(land_bosses[pool_land]) or pool
            cands = [c for c in cands if c in units_srv] or pool
            bosses = [rng.choice(cands)]
            if rng.random() < 0.4:
                bosses.append(rng.choice(cands))
        # the drops: the zone's, else the land's items
        drop_names = (zone or {}).get("drops") or []
        drop_ids = [items_by_name[d] for d in drop_names if d in items_by_name]
        zel_per = max(10, int(info.get("zel", 0)) // max(1, n * 2))
        karma_per = max(5, int(info.get("karma", 0)) // max(1, n * 2))

        def monster(uid, role, hp_override=None):
            srv = units_srv[uid]
            prof = unit_profile.get(uid, {})
            stats = {}
            for s in ("hp", "atk", "def"):
                mx = srv.get(s, [1, 1])[1]
                stats[s] = max(1, int(mx * factor(fits[(role, s)], xp)))
            if hp_override:
                stats["hp"] = hp_override
            acts = prof.get("act counts") or ("2~3" if role == "boss" else "1~2")
            resists = prof.get("condition resists") or ({"poison%": 50, "weaken%": 50, "sick%": 50, "injury%": 50, "curse%": 50, "paralysis%": 50} if role == "boss" else {})
            drops = []
            for d in rng.sample(drop_ids, min(2, len(drop_ids))):
                drops.append({"_name": next(k for k, v in items_by_name.items() if v == d), "percent": 3.0 if role == "mob" else 10.0, "drop rest?": 0})
            return {
                "_name": english.get(uid, srv.get("name", uid)),
                "act counts": acts,
                "ai id": prof.get("ai id", default_ai[role]),
                "ai": prof.get("ai"),
                "condition resists": resists,
                "desc": "",
                "element": ELEMENT_NAMES.get(int(srv.get("element", 1)), "fire"),
                "item drop": drops,
                "karma drop count": 5, "karma max drop": karma_per * (3 if role == "boss" else 1),
                "passive": prof.get("passive") or [],
                "skills": prof.get("skills") or [],  # the captured skills, so the captured AI's "set skill" terms resolve
                "stats": stats,
                "unit id": int(uid),
                "zel drop count": 5, "zel max drop": zel_per * (3 if role == "boss" else 1),
            }

        battles = []
        for b in range(1, n + 1):
            if b < n:
                k = 2 if n <= 3 or rng.random() < 0.5 else 3
                battles.append({"monsters": [monster(rng.choice(pool), "mob") for _ in range(k)]})
            else:
                ms = [monster(uid, "boss", boss_hp.get(i)) for i, uid in enumerate(bosses) if uid in units_srv]
                if not ms:
                    ms = [monster(rng.choice(pool), "boss")]
                if len(ms) == 1 and n > 1:
                    ms.append(monster(rng.choice(pool), "mob"))
                battles.append({"monsters": ms})
        out = bb.build(mid, info, battles, units_mst, items_by_name, ai_json, warnings.append, unlocked_by.get(mid, ()), rewards.get(mid, ()), skills, skill_names, None, unit_bbs, named_ids, True)
        out["synthesized"] = source
        with open(path, "w", encoding="utf-8", newline="\n") as fo:
            json.dump(out, fo, ensure_ascii=False, indent=1)
        written["wiki" if source == "wiki" else "land"] += 1
    for w in sorted(set(warnings)):
        print("warning:", w)
    print(f"synthesized {written['wiki']} missions from the wiki pages and {written['land']} from the land pools into {a.out}; {skipped} already had data")


if __name__ == "__main__":
    main()
