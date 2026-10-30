# bfanew: Brave Frontier phased private server

Context for any Claude session working here. Read this first, then `GAP_ANALYSIS.md`.

## Goal

A private server for **Brave Frontier Global** that is:
- **faithful**: the original v2.19.6.0 client, original data and assets, like the fan revival "Brave Frontier Revive" (bfrevive.net);
- **phased**: content released bit by bit, like "Brave Frontier: Reborn" (rebornbf.com, a browser remake that isn't faithful).

The base is the open-source emulator **decompfrontier** (C++/Drogon, AGPL-3.0). Revive also started from it, but its code isn't public.

### Decisions so far
- **Phase-1 cutoff:** everything Global had by the **end of Oct 2014**. That's 1,371 unit IDs and 1,292 mission IDs from `data/*_timeline.json` with `first_seen <= "2014-10"`; it includes Vriksha.
- **Client:** Android APK v2.19.6.0. The WinRT PC client link in the decompfrontier docs is dead.
- **Build:** Windows, Visual Studio 2026 Build Tools, Ninja preset.

## Layout (`D:\.Code\bravefrontier`)

```
bfanew/                      <- this repo (git, branch main)
  CLAUDE.md                  <- this file
  GAP_ANALYSIS.md            <- full analysis: server state, phasing design, gap table, plan
  data/unit_timeline.json    <- unit id -> first month seen in Global datamine (2,940 ids)
  data/mission_timeline.json <- mission id -> first month seen (3,744 ids)
  data/build_timeline.py     <- script that built them (args: <repo> <file.json> <out.json>)
  setup/CMakeUserPresets.json<- copy of the presets file placed in decompfrontier/server
decompfrontier/              <- upstream clones, read-only reference except local build files
  server/                    <- C++ emulator. Built and running (see below)
    packet-generator/        <- submodule, checked out at pinned 61fedcc
    gimuserver/packets/all.hpp  <- GENERATED (not in git), regenerate after .kdl changes
    CMakeUserPresets.json    <- ours (VS 2026 / Ninja presets), untracked
    deploy/                  <- working dir to run the server from
      config.json            <- Drogon + GimuServer config (listens 127.0.0.1:9960)
      system/*.json          <- 45 server-side master tables sent at login
      game_content/content/  <- assets (116 folders), from 21900.zip -> assets.zip
      game_content/mst/      <- 147 encrypted MST .dat tables (client downloads these)
    out/build/debug-win64-ninja/ <- build output
  packet-generator/          <- standalone clone (Rust, nightly)
  offline-proxy/             <- Windows-only DLL proxy for the PC client (not usable for Android)
  offline-diff/              <- empty scaffolding
  docs/                      <- decompfrontier.github.io sources (architecture, TODO, tutorials)
assets/                      <- extracted from 21900.zip (BluuArc archive, snapshot 2022-04-03)
  assets.zip                 <- already unpacked into server/deploy/game_content
  mst-decoded.zip            <- 1-mst (raw), 2-mst-decoded (JSON), 3-mst-translated (readable keys)
  old-mst.zip                <- ~2,400 older MST files, client 143->180, Jan 2016 -> Aug 2017 only
  README.txt, asset-structure.txt
21900.zip                    <- 14 GB original archive (videos/GIFs/BF2 not extracted)
brave-frontier-2-19-6-0.apk  <- original APK, SHA-256 346d29478b3a2a8246c79267abf7c5d9e1e785b10d63677dbdf93fb14d139bf9
```

Not cloned: `decompfrontier/client` (a 180 MB client decompilation, not needed so far).

## Environment (Windows)

- **Visual Studio 2026 Build Tools** (`C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools`), MSVC 14.50 / cl 19.50. Use the **Developer PowerShell for VS 2026**.
- **vcpkg** at `D:\.Code\vcpkg`, `VCPKG_ROOT` set for the user, `integrate install` done. Dependencies (drogon 1.9.12, cryptopp, sqlite3, glaze 6.5.1, openssl 3.6.1…) are built and cached.
- **Rust** via rustup. The packet generator auto-installs **nightly** (`rust-toolchain.toml`).
- Windows `tar` handles zip files and is much faster than anything else for the big archives.

## Commands that work

```powershell
# regenerate packet classes (after editing packet-generator/assets/**/*.kdl)
cd D:\.Code\bravefrontier\decompfrontier\server\packet-generator
cargo run -- generate --cxx --glaze -i assets/all.kdl -o ../gimuserver/packets
#   (don't use generate.py: it swallows errors)

# configure + build (Developer PowerShell for VS 2026)
cd D:\.Code\bravefrontier\decompfrontier\server
cmake --preset debug-win64-ninja
cmake --build --preset debug-win64-ninja

# run (must be started from deploy/ because config.json and game_content are relative)
cd deploy
..\out\build\debug-win64-ninja\standalone_frontend\gimuserverw.exe

# smoke test (verified 2026-10-30)
curl.exe http://127.0.0.1:9960/dls                                   # -> {"SREE":"..."}
curl.exe -o NUL -w "%{http_code}\n" http://127.0.0.1:9960/mst/Ver1084_2r9cNSdt_1.dat   # -> 200
```

Server routes: `/dls`, `/accounts/guest/login/`, `/bf/gme/action.php` (POST, encrypted dispatch by request id), `/bf/gme/featureCheck.php`, `/bf/gme/action/getServerTime.php`, `/bf/gme/action/Daily_login.php`, plus static `/content/*`, `/mst/*`, `/bf/web/*`.

## Server state in one paragraph

Login handshake, encryption (SREE/GME AES) and static serving work. There are 11 GME handlers, several of them stubs. **Every player is the same hard-coded user** (`12345678`, "OfflineMod!", one fake unit). The account-creation SQL is `#if 0`, and the DB has a single unused `userinfo` table. **Quests, rewards, summons, fusion/evolution, items, town, arena, gifts and friends are not implemented.** Handler registry: `gimuserver/gme/GmeControllerHandlers.cpp` (`REGISTER(id, func, aesKey)`). Login-time tables are loaded in `gimuserver/drogon/ServerCache.cpp` from `deploy/system/*.json`. The downloaded-MST version list is `deploy/system/version_info.json` (169 tables with version numbers). See `GAP_ANALYSIS.md` for the full gap table.

## Phasing design (summary)

1. **Login-time tables** (`deploy/system/*.json`: summons, banners, daily dungeons, campaigns): one folder per phase, plus a `phase` config switch.
2. **Downloaded MSTs** (`F_UNIT_MST`, `F_MISSION_MST`, skills, evos…): serve phase-filtered copies and bump their versions in `version_info.json`, so the client re-downloads them. Reading is solved by `mst-decoded.zip`. Still to do: **re-encrypting** to the `.dat` format, and a filter that keeps references consistent (unit -> skills/LS/ES/evo mats/AI).
3. **Feature flags** (`deploy/system/features.json`: `raid`, `colosseum_enable`, `guild`, `frontierhunter`, `randall`, `challenge_arena`, …; also `F_FUNCTION_RELEASE_MST` and the `feature_gate` field). **Unverified** whether the client hides menus cleanly. Test one by one.
4. **Server-side enforcement**: handlers reject out-of-phase IDs.

Historical balance: 2014 stats come from the datamine snapshots (`cheahjs/bravefrontier_data`, Aug 2014 → Nov 2021, parsed fields only). `old-mst.zip` has raw tables only from 2016–2017.

To rebuild the timelines: `git clone --filter=blob:none https://github.com/cheahjs/bravefrontier_data`, then `python data/build_timeline.py bravefrontier_data info.json data/unit_timeline.json` (and the same with `missions.json`). It takes a while, because the unit file is 80 MB × ~80 monthly snapshots.

## Android client: findings so far (next task)

- The APK contains `lib/arm64-v8a/libgame.so` (42.5 MB) and `lib/armeabi-v7a/libgame.so` (27.6 MB), Cocos2d-x v2, networking via libcurl in native code. There's also a Java Apache HttpClient fork (`ch/boye/httpclientandroidlib`).
- Server hosts written into `libgame.so` (from `strings`): `api-sl.bfww.gumi.sg`, `api-prod-player.bfww.gumi.sg`, `ios.bfww.gumi.sg`, `https://api-sl.gl.gumi.sg/`. **The game-server and CDN addresses come from our `/dls` reply** (`ServerCache.cpp` builds it from the listener host), so only these entry points need patching.
- Plan (from the old emufrontier README): hex-patch those strings (`https://` → `http://`, host → our server, same byte length, NUL-padded), patch **both** ABIs, re-zip, zipalign, re-sign (v1+v2), install.
- **Still to check:** targetSdk and minSdk in the manifest (decides whether v1 signing is enough and whether cleartext HTTP is blocked for the Java client), which host each code path uses, and whether anything checks certificates.
- **Still to decide with the user:** a real phone or an Android emulator. An emulator needs ARM translation, since the APK has no x86 libs.
- **Before a device can connect:** change `deploy/config.json` listener `address` from `127.0.0.1` to `0.0.0.0` (or the LAN IP). From the Android emulator the PC is reachable at `10.0.2.2`.

## Next steps (in order)

1. Android patch: inspect the manifest and `libgame.so` string references, write a **repeatable patch script** in `bfanew/tools/` (Python), produce a signed APK, and get the client to log in against the local server, using the server request log (`deploy/log/`, TRACE level) to see what it asks for.
2. Fork decompfrontier/server into bfanew (keep AGPL, keep upstream history if possible), and add the `phase` config + `phases/NN/` folders for login-time tables.
3. MST tooling: decode/encode `.dat` (start from `mst-decoded.zip` and `BfCrypt.cpp`), plus the phase filter driven by `data/*_timeline.json`.
4. Core loop: accounts and persistence → unit box → quests and rewards → summon → fusion/evolution.
5. Test feature flags system by system.

## Conventions

- Don't modify the `decompfrontier/*` clones beyond local build files. Real changes go into bfanew (step 2 forks the server there).
- Git identity: `Sylvain Franco <sylvain.franco@outlook.fr>`. End commit messages with:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  ```
- Non-commercial fan project. Server code stays AGPL-3.0, so source must be offered to anyone using a public server. The APK, art and data belong to Alim/gumi.
