# bfanew

A fan-made private server for **Brave Frontier Global** (Android client 2.19.6.0), with two goals:

- **faithful**: the original client, data and assets, like "Brave Frontier Revive";
- **phased**: content released bit by bit, starting from what Global had at the end of October 2014.

It is built on [decompfrontier/server](https://github.com/decompfrontier/server), an open-source
emulator of the game's backend (C++ / Drogon). The server lives in `server/` as a fork imported
with `git subtree`, so upstream history is kept and upstream changes can still be pulled.

## State (October 2026)

- The patched Android client logs in, reaches the home screen, summons (Honor / Rare Summon
  with the 2013/2014 art) and plays quests: world map, mission list, battles with the 2014
  enemy data (from the community datamine captures), result screen.
- Accounts are persistent (SQLite): one per device, with units, squads, wallet, level, energy,
  cleared missions and unlocked places; squads can be edited and units fused. Evolution,
  selling, items, town, arena and friends are not implemented yet (see `GAP_ANALYSIS.md`).
- `CLAUDE.md` is the working log: layout, build and run commands, what the client does on the
  wire, what was patched and why, and the next steps. Read it first.

## Layout

| Path | What |
|---|---|
| `server/` | The server fork (C++, Drogon, vcpkg). `server/packet-generator/` is the vendored packet definition generator (Rust). |
| `server/deploy/system/*.json` | Server-side master tables sent at login; this is where phase content is edited. |
| `tools/patch_apk.py` | Points the 2.19.6.0 APK at a private server (host strings, `https` to `http`), then aligns and signs it with the Android SDK tools. |
| `tools/dev.py` | The dev loop: build, serve, reload data, drive the game on BlueStacks, read the request log. |
| `tools/build_battles.py` | Builds `server/deploy/battle/*.json` (per-mission enemy data for MissionStart) from the community datamine's 2014 captures. |
| `tools/build_units.py` | Builds `server/deploy/units.json`, the server-side unit table (stats, exp pattern, max level) from the decoded unit MST. |
| `server/deploy/battle/` | Those files, 204 missions so far. |
| `data/*_timeline.json` | First month each unit / mission appeared in Global, derived from the community datamine history; drives the phase cutoff. |
| `GAP_ANALYSIS.md` | What the upstream server does and does not do, and how phasing fits. |

Not in this repo: the APK, the game assets (`content/`, `mst/`), and the signing key. They belong
to Alim / gumi and are not redistributed here.

## Building and running

Windows, Visual Studio 2026 Build Tools, vcpkg, Rust nightly for the generator. The exact
commands are in `CLAUDE.md` ("Commands that work"). In short:

```powershell
cd server\packet-generator
cargo run -- generate --cxx --glaze -i assets/all.kdl -o ../gimuserver/packets
cd ..
cmake --preset debug-win64-ninja
cmake --build --preset debug-win64-ninja
cd deploy
..\out\build\debug-win64-ninja\standalone_frontend\gimuserverw.exe
```

The client needs an Android device or emulator on the same network; `tools/patch_apk.py`
produces the APK to install.

## License

`server/` is a fork of decompfrontier/server and stays under the **GNU AGPL-3.0**
(`server/LICENSE`). The rest of this repository is under the same license. This is a
non-commercial fan project; Brave Frontier, its client, art and data are the property of
Alim Co., Ltd. and gumi Inc.
