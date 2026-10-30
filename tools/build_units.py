#!/usr/bin/env python3
"""Build server/deploy/units.json, the server-side unit table, from the decoded F_UNIT_MST
(assets/mst-decoded.zip, 3-mst-translated/F_UNIT_MST_*.json).

The server never reads the downloaded MSTs (they are encrypted .dat files served as-is), but
fusion, level-ups and summons need the unit stats. This keeps the columns the server uses:
rarity, cost, element, max level, exp pattern, base/lord stats, adjust exp, skills, sell price.

Usage:
  python tools/build_units.py --mst ..\\assets\\mst-decoded.zip --out server\\deploy\\units.json
"""
import argparse
import json
import zipfile


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mst", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    units = {}
    with zipfile.ZipFile(a.mst) as z:
        for n in sorted(z.namelist()):
            if not n.startswith("3-mst-translated/F_UNIT_MST"):
                continue
            for u in json.loads(z.read(n)):
                uid = u.get("unitId")
                if not uid or uid == "0":
                    continue

                def i(k, d=0):
                    try:
                        return int(u.get(k, d) or d)
                    except ValueError:
                        return d

                units[uid] = {
                    "name": u.get("unitName", ""),
                    "rarity": i("rarity"),
                    "cost": i("unitCost"),
                    "element": i("element"),
                    "max_level": i("unitMaxLevel"),
                    "exp_pattern": i("unitExpPatternId"),
                    "hp": [i("unitBaseHp"), i("unitLordHp")],
                    "atk": [i("unitBaseAtk"), i("unitLordAtk")],
                    "def": [i("unitBaseDef"), i("unitLordDef")],
                    "rec": [i("unitBaseRec"), i("unitLordRec")],
                    "imp_caps": u.get("unitImpCaps", ""),
                    "adjust_exp": i("unitAdditionalXpBoost"),
                    "ls_id": i("lsId"),
                    "bb_id": i("bbId"),
                    "es_id": i("esId"),
                    "sell_price": i("sellPrice"),
                    "group": i("group"),
                }
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        json.dump(units, f, ensure_ascii=False, indent=0, sort_keys=True)
    print(f"{len(units)} units written to {a.out}")


if __name__ == "__main__":
    main()
