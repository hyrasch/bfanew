#!/usr/bin/env python3
"""Build the server-side unit tables from the decoded MSTs (assets/mst-decoded.zip,
3-mst-translated/F_UNIT_MST_*.json and F_UNIT_EVO_MST_*.json).

The server never reads the downloaded MSTs (they are encrypted .dat files served as-is), but
fusion, evolution, selling, level-ups and summons need the unit data. This keeps the columns
the server uses:
  units.json  unit id -> rarity, cost, element, max level, exp pattern, base/lord stats,
              adjust exp, skills, sell price, family (group)
  evos.json   unit id -> evolved unit id, zel, karma, materials [{type, id}] (type 1 = unit)
  items.json  item id -> name, type, rarity, sell price, max stack, max equipped, thumbnail

Usage:
  python tools/build_units.py --mst ..\\assets\\mst-decoded.zip --out server\\deploy\\units.json --evos server\\deploy\\evos.json --items server\\deploy\\items.json
"""
import argparse
import json
import zipfile


def to_int(v, d=0):
    try:
        return int(v or d)
    except ValueError:
        return d


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mst", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--evos", help="also write the evolution table (F_UNIT_EVO_MST) to this file")
    ap.add_argument("--items", help="also write the item table (F_ITEM_MST) to this file")
    a = ap.parse_args()

    units = {}
    evos = {}
    items = {}
    with zipfile.ZipFile(a.mst) as z:
        for n in sorted(z.namelist()):
            if n.startswith("3-mst-translated/F_UNIT_MST"):
                for u in json.loads(z.read(n)):
                    uid = u.get("unitId")
                    if not uid or uid == "0":
                        continue
                    units[uid] = {
                        "name": u.get("unitName", ""),
                        "rarity": to_int(u.get("rarity")),
                        "cost": to_int(u.get("unitCost")),
                        "element": to_int(u.get("element")),
                        "max_level": to_int(u.get("unitMaxLevel"), 1),
                        "exp_pattern": to_int(u.get("unitExpPatternId")),
                        "hp": [to_int(u.get("unitBaseHp")), to_int(u.get("unitLordHp"))],
                        "atk": [to_int(u.get("unitBaseAtk")), to_int(u.get("unitLordAtk"))],
                        "def": [to_int(u.get("unitBaseDef")), to_int(u.get("unitLordDef"))],
                        "rec": [to_int(u.get("unitBaseRec")), to_int(u.get("unitLordRec"))],
                        "imp_caps": u.get("unitImpCaps", ""),
                        "adjust_exp": to_int(u.get("unitAdditionalXpBoost")),
                        "ls_id": to_int(u.get("lsId")),
                        "bb_id": to_int(u.get("bbId")),
                        "es_id": to_int(u.get("esId")),
                        "sell_price": to_int(u.get("sellPrice")),
                        "group": to_int(u.get("group")),
                    }
            elif n.startswith("3-mst-translated/F_ITEM_MST"):
                for it in json.loads(z.read(n)):
                    iid = it.get("itemId")
                    if not iid or iid == "0":
                        continue
                    items[iid] = {
                        "name": it.get("itemName", ""),
                        "type": to_int(it.get("itemType")),
                        "rarity": to_int(it.get("rarity")),
                        "sell_price": to_int(it.get("sellPrice")),
                        "max_stack": to_int(it.get("itemMaxStack"), 999),
                        "max_equipped": to_int(it.get("itemMaxEquipped")),
                        "thumbnail": it.get("itemThumbnail", ""),
                    }
            elif n.startswith("3-mst-translated/F_UNIT_EVO_MST"):
                for e in json.loads(z.read(n)):
                    mats = []
                    for i in range(1, 11):
                        mid = e.get(f"evoMaterialId{i}", "0")
                        if mid and mid != "0":
                            mats.append({"type": to_int(e.get(f"evoMaterialType{i}"), 1), "id": to_int(mid)})
                    evos[e["unitId"]] = {
                        "evo_unit_id": to_int(e.get("evoUnitId")),
                        "zel": to_int(e.get("zel")),
                        "karma": to_int(e.get("karma")),
                        "materials": mats,
                    }

    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        json.dump(units, f, ensure_ascii=False, indent=0, sort_keys=True)
    print(f"{len(units)} units written to {a.out}")
    if a.evos:
        with open(a.evos, "w", encoding="utf-8", newline="\n") as f:
            json.dump(evos, f, ensure_ascii=False, indent=0, sort_keys=True)
        print(f"{len(evos)} evolutions written to {a.evos}")
    if a.items:
        with open(a.items, "w", encoding="utf-8", newline="\n") as f:
            json.dump(items, f, ensure_ascii=False, indent=0, sort_keys=True)
        print(f"{len(items)} items written to {a.items}")


if __name__ == "__main__":
    main()
