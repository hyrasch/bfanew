#!/usr/bin/env python3
"""
build_pools.py - the summon pools of the current phase: deploy/system/phases/<NN>/gacha_pools.json.

A pool is built from the phase's units (deploy/phase.json) and the served F_UNIT_MST:
  Rare Summon (gacha 2000): unitGettingType 2 ("rare summon family"), base forms only (a unit that
                            no evolution of the phase produces), plain units (unitKind 0);
  Honor Summon (gacha 1000): unitGettingType 3 (common / quest units), base forms, unitKind 0.
Rates are per rarity and come from data/overrides.json "pools" (a rarity without a rate is left
out: phase 1 has no 5* base in the Rare Summon and no 4* in the Honor Summon, those are later
collaborations the timeline matched by name). GachaAction draws a rarity by weight, then a unit
uniformly, stats from deploy/units.json.

    python tools/build_pools.py        # run by build_overrides.py too
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

# raw F_UNIT_MST keys (readable names from mst-decoded.zip's 3-mst-translated)
K_UNIT, K_RARITY, K_GETTING, K_KIND = "pn16CNah", "7ofj5xa1", "6yHMXYv1", "xP9oi5Z2"
K_EVO_FROM, K_EVO_TO = "pn16CNah", "74VFwuTd"


def build(pools_cfg: dict, phase_id: int) -> Path:
    phase = json.loads((mst.DEPLOY / "phase.json").read_text(encoding="utf-8"))
    kept = set(phase["units"])
    units = {int(r[K_UNIT]): r for r in mst.decode_table("F_UNIT_MST", mst.archive_version("F_UNIT_MST"))}
    evos = mst.decode_table("F_UNIT_EVO_MST", mst.archive_version("F_UNIT_EVO_MST"))
    targets = {int(r[K_EVO_TO]) for r in evos if int(r[K_EVO_FROM]) in kept}

    out = {"_doc": "Built by tools/build_pools.py from deploy/phase.json and F_UNIT_MST: per summon (gacha id), the rate of each rarity (weights) and the unit ids of that rarity. GachaAction draws the rarity, then a unit uniformly.", "pools": {}}
    for gacha_id, cfg in pools_cfg.items():
        rates = {str(k): int(v) for k, v in cfg["rates"].items()}
        by_rarity: dict[str, list[int]] = collections.defaultdict(list)
        for uid in sorted(kept):
            u = units.get(uid)
            if not u or str(u[K_GETTING]) != str(cfg["getting_type"]) or str(u[K_KIND]) != "0" or uid in targets:
                continue
            r = str(u[K_RARITY])
            if r in rates:
                by_rarity[r].append(uid)
        empty = [r for r in rates if not by_rarity.get(r)]
        if empty:
            print(f"  gacha {gacha_id}: no unit of rarity {empty}, dropping those rates")
            rates = {r: w for r, w in rates.items() if r not in empty}
        out["pools"][str(gacha_id)] = {"name": cfg.get("name", ""), "rates": rates, "units": dict(sorted(by_rarity.items()))}
        print(f"  gacha {gacha_id} ({cfg.get('name', '')}): " + ", ".join(f"{r}*: {len(v)} units at {rates[r]}" for r, v in sorted(by_rarity.items())))
    dst = mst.DEPLOY / "system" / "phases" / f"{phase_id:02}" / "gacha_pools.json"
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text(json.dumps(out, indent=1) + "\n", encoding="utf-8")
    return dst


def main():
    cfg = json.loads((DATA / "overrides.json").read_text(encoding="utf-8"))
    dst = build(cfg["pools"], cfg["phase"].get("id", 0))
    print(f"  -> {dst.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
