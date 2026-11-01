# bfanew: Brave Frontier phased private server

Context for any Claude session working here. Read this first, then `GAP_ANALYSIS.md`.

## Goal

A private server for **Brave Frontier Global** that is:
- **faithful**: the original v2.19.6.0 client, original data and assets, like the fan revival "Brave Frontier Revive" (bfrevive.net);
- **phased**: content released bit by bit, like "Brave Frontier: Reborn" (rebornbf.com, a browser remake that isn't faithful).

The base is the open-source emulator **decompfrontier** (C++/Drogon, AGPL-3.0). Revive also started from it, but its code isn't public.

### Decisions so far
- **Phase-1 cutoff:** everything Global had by the **end of Oct 2014**: `data/*_timeline.json` entries with `first_seen <= "2014-10"`, i.e. **724 units and 696 missions** (lands Mistral to Vriksha plus 235 Vortex missions; `tools/build_phase.py`). The "1,371 / 1,292" counted earlier added the name-keyed Aug-2014 entries to the id-keyed ones for the same units.
- **Client:** Android APK v2.19.6.0. The WinRT PC client link in the decompfrontier docs is dead.
- **Build:** Windows, Visual Studio 2026 Build Tools, Ninja preset.

## Layout (`D:\.Code\bravefrontier`)

```
bfanew/                      <- this repo (git, branch main, origin = https://github.com/hyrasch/bfanew, public)
  CLAUDE.md                  <- this file
  GAP_ANALYSIS.md            <- full analysis: server state, phasing design, gap table, plan
  server/                    <- OUR FORK of decompfrontier/server (git subtree, upstream 7cc0ebd, AGPL-3.0)
    packet-generator/        <- vendored (git subtree, upstream 61fedcc), no longer a submodule
    gimuserver/packets/all.hpp  <- GENERATED (ignored), regenerate after .kdl changes
    CMakeUserPresets.json    <- tracked here (VS 2026 / Ninja presets)
    deploy/config.json       <- two listeners: 192.168.1.14:9960 (advertised by /dls), 127.0.0.1:9960
    deploy/game_content      <- JUNCTION to decompfrontier/server/deploy/game_content (not in git)
    out/build/debug-win64-ninja/ <- build output (ignored)
  tools/patch_apk.py         <- Android client patcher (see "Android client")
  data/unit_timeline.json    <- unit id -> first month seen in Global datamine (2,940 ids)
  data/mission_timeline.json <- mission id -> first month seen (3,744 ids)
  data/build_timeline.py     <- script that built them (args: <repo> <file.json> <out.json>)
  setup/CMakeUserPresets.json<- copy of the presets file placed in decompfrontier/server
decompfrontier/              <- upstream clones, now READ-ONLY reference (the fork above is what runs)
  server/                    <- C++ emulator (upstream). Its deploy/game_content holds the assets
    packet-generator/        <- submodule, checked out at pinned 61fedcc
    gimuserver/packets/all.hpp  <- GENERATED (not in git), regenerate after .kdl changes
    CMakeUserPresets.json    <- ours (VS 2026 / Ninja presets), untracked
    deploy/                  <- working dir to run the server from
      config.json            <- Drogon + GimuServer config (listens 127.0.0.1:9960)
      system/*.json          <- 45 server-side master tables sent at login (the base set)
      system/phases/01/      <- phase-1 overrides of those tables (gacha_info, vortex_schedule); a file
                                here wins over the base one while deploy/phase.json says id 1
      game_content/content/  <- assets (116 folders), from 21900.zip -> assets.zip
      game_content/mst/      <- 147 encrypted MST .dat tables (client downloads these)
      overrides/             <- built by tools/build_overrides.py (gitignored): files served before
                                game_content (patched sgtext_en, re-encoded F_RESOURCE_MST)
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
cd D:\.Code\bravefrontier\bfanew\server\packet-generator
cargo run -- generate --cxx --glaze -i assets/all.kdl -o ../gimuserver/packets
#   (don't use generate.py: it swallows errors)

# configure + build (Developer PowerShell for VS 2026, or in any PowerShell first run
#   & 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -SkipAutomaticLocation)
cd D:\.Code\bravefrontier\bfanew\server
cmake --preset debug-win64-ninja
cmake --build --preset debug-win64-ninja

# run (must be started from deploy/ because config.json and game_content are relative)
cd deploy
..\out\build\debug-win64-ninja\standalone_frontend\gimuserverw.exe

# smoke test (verified 2026-10-30 on the fork)
curl.exe http://127.0.0.1:9960/dls                                   # -> {"SREE":"..."}
curl.exe -o NUL -w "%{http_code}\n" http://127.0.0.1:9960/mst/Ver1084_2r9cNSdt_1.dat   # -> 200
```

Server routes: `/dls`, `/accounts/guest/login/`, `/bf/gme/action.php` (POST, encrypted dispatch by request id), `/bf/gme/featureCheck.php`, `/bf/gme/action/getServerTime.php`, `/bf/gme/action/Daily_login.php`, plus static `/content/*`, `/mst/*`, `/bf/web/*`.

## Server state in one paragraph

Login handshake, encryption (SREE/GME AES) and static serving work. **Players are persistent
since 2026-10-30** (`gimuserver/db/Player.hpp`, SQLite `deploy/gme.sqlite` through Drogon's
db client, tables created by the migration in `gimuserver/db/MigrationManager.cpp`): one account
per device (`/accounts/guest/login/` keys on the client's `vid`), one player per account created
at the first Initialize from the `NewPlayer.hpp` template (wallet from `config.json`
`initial_*`, the starter unit in every squad, gate 1 / Mistral / area 100 / dungeons 1 and 10 /
missions 1, 2, 10 permitted, tutorial missions cleared). Stored: wallet, level, exp, energy (regen 1
point per `energy_regen_seconds`, 180), units (`UserUnitInfo` as JSON), squads, cleared missions,
permitted places, mission runs, the warehouse (item pickups) and the items equipped for quests.
Handlers that write: GachaAction (units, gems / honor points), MissionStart (energy, run),
MissionEnd (rewards, level, clears, unlocks, item pickups and items used), DeckEdit (squads,
helper squads, handle name), UnitMix (fusion), UnitEvo (evolution, unit and item materials),
UnitSell, ItemEdit (equipped items), ItemSell, ItemMix (synthesis), ShopUse (energy refill, arena
orb refill, slot extensions, all free), TownFacilityUpdate (facility / location levels), TownUpdate (village item collection), UpdatePermitPlaceInfo (permit refresh, the Vortex schedule), the Arena (ArenaStart / Matching / BattleStart / BattleEnd / Retire / Restart, GetPlayerInfo; see "Arena"). Stubs: DungeonEventUpdate (story events watched,
not stored), UpdateEventInfo (home screen 5-minute poll), friends (FriendGet / Apply / Agree / Refuse / Delete / Search / RandomSearch / Favorite, helpers and friend points; see "Friends"). Not implemented: gifts (the Shop tab shows, buying gems is off). The village (Town tab) works again: its crash was a content-archive gap, not a packet (see
"Village crash" under "Items / warehouse"). Handler registry:
`gimuserver/gme/GmeControllerHandlers.cpp` (`REGISTER(id, func, aesKey)`). Login-time tables are
loaded in `gimuserver/drogon/ServerCache.cpp` from `deploy/system/*.json`. The downloaded-MST
version list is `deploy/system/version_info.json` (169 tables). Per-mission battle data is
`deploy/battle/<id>.json`. See `GAP_ANALYSIS.md` for the full gap table.

## Phasing design (summary)

1. **Login-time tables** (`deploy/system/*.json`: summons, banners, daily dungeons, campaigns): one folder per phase, plus a `phase` config switch. **Done 2026-10-31**: `deploy/system/phases/<NN>/<table>.json` wins over `deploy/system/<table>.json`, NN = `data/overrides.json` `phase.id` (written to `deploy/phase.json`, read by `ServerCache::Setup` before any table; `reload` re-reads). Phase 1 holds `gacha_info.json` (the two launch banners; the base file is upstream's six) and `vortex_schedule.json`.
2. **Downloaded MSTs** (`F_UNIT_MST`, `F_MISSION_MST`, skills, evos…): serve phase-filtered copies and bump their versions in `version_info.json`, so the client re-downloads them. **Done 2026-10-31**: `tools/mst.py` (decode / encode), `tools/build_phase.py` (the filter, see "MST tables: the .dat format"), served from `deploy/overrides/mst/`.
3. **Feature flags** (`deploy/system/features.json`: `raid`, `colosseum_enable`, `guild`, `frontierhunter`, `randall`, `challenge_arena`, …; also the function release list and the `feature_gate` field). **Done 2026-10-31** for phase 1: `deploy/system/phases/01/features.json` (31 flags off) + `release_info.json` (function 1 closed = the village's Randall icon), see "Feature gating". Leftover: the Daily Login Spin icon (no flag; it opens with 0 spins).
4. **Server-side enforcement**: handlers reject out-of-phase IDs. **Done 2026-10-31** for missions, evolutions and summons (`deploy/phase.json` -> `ServerCache::phase()`, see "Phase enforcement" under "MST tables").

Historical balance: 2014 stats come from the datamine snapshots (`cheahjs/bravefrontier_data`, Aug 2014 → Nov 2021, parsed fields only). `old-mst.zip` has raw tables only from 2016–2017.

To rebuild the timelines: `git clone --filter=blob:none https://github.com/cheahjs/bravefrontier_data`, then `python data/build_timeline.py bravefrontier_data info.json data/unit_timeline.json` (and the same with `missions.json`). It takes a while, because the unit file is 80 MB × ~80 monthly snapshots.

## Dev loop (use this, not the manual commands below)

`tools/dev.py` does the whole iteration from one place (Python 3.14, no extra packages):

```powershell
cd D:\.Coderavefrontierfanew
python tools/dev.py serve                 # run the server in THIS terminal (Ctrl-C stops it); logs visible
python tools/dev.py build [--configure]   # VS dev shell + Ninja; --configure after adding a .cpp
python tools/dev.py reload                # GET /bfanew/reload: re-read deploy/system/*.json, no restart
python tools/dev.py bump M_GACHA_MST      # bump a table version in version_info.json (+reload)
python tools/dev.py client --to summon    # relaunch the game on BlueStacks and drive it there (~30 s)
python tools/dev.py client --to mission   # ... or to the world map (quest) / the first mission list (mission)
python tools/dev.py summon [--rare] [--multi]   # from the Summon scene: summon, open the door (~25 s)
python tools/dev.py shot name             # screenshot -> ..\build\shots\name.png (+ window frame)
python tools/dev.py tapk tab_unit | tap X Y | swipe ... | key back
python tools/dev.py logs -n 15 [--show ID]   # latest requests with handler names, errors flagged
python tools/dev.py restart / stop / start   # detached server (when not using `serve`)
```

- **Data edits need no build and no restart**: edit `server/deploy/system/*.json`, `reload`.
  Downloaded MSTs still need their version bumped (`bump`) before the client re-reads them.
- `serve` runs the exe in the foreground; after a `build`, Ctrl-C it and run `serve` again
  (`restart` is for the detached mode and would not restart a `serve` tab).
- The client driver waits on the server's request log (DLS -> guest login -> HomeInfo)
  instead of fixed sleeps, taps the title until the login shows up, and dismisses whatever
  Android puts over the game by the focused window (`dismiss_dialogs`: a gms window = the Play
  Games sign-in or "Create a Play Games profile" prompt -> Back, then its Cancel; the
  permissioncontroller = "Allow Brave Frontier to make and manage phone calls?" -> ALLOW, tapped
  on the window frame, not the content area). **`--fresh`** (`pm clear`, then the first run)
  is a polled cycle too: the title loads first (DLS request), then Terms (checkbox, I Agree),
  the phone-permission notice (OK), Android's permission dialog, the title tap; each tick
  dismisses a dialog or sends the next tap of the cycle, every tap being harmless on the other
  screens, until the guest login shows up (240 s cap); then the 147-table MST download, during
  which the "Background DLC Warning Prompt" gets a No every 6 s, until HomeInfo (420 s cap).
  Verified 2026-10-31: fresh install -> home in 85 s, hands-off (the old version slept 12 s and
  tapped blind, and missed every one of those screens).
- Taps are fractions of the game's content area, computed from the window frame reported by
  `dumpsys window windows` (`frame=[x0,y0][x1,y1]`): the whole frame when it is phone-like
  (<= 1.8 tall), else the centred 2:3 rectangle (the default BlueStacks window letterboxes).
  BlueStacks resets the window to its default size on every launch. If a tap misses, `shot`
  prints the frame and content rect; measure on the PNG and fix `COORDS`.
- Startup popups are gone from the data (phase 1 has no daily tasks and no login campaign:
  `TEMP_daily_tasks*.json` lists emptied, `login_campaign*.json` zeroed, `dailytask_notify`
  0, `UserInfo` campaign info zeroed), so the home screen is reachable without extra taps.
- Rough timings now: build 10-60 s (only for C++), reload < 1 s, client to Summon 30 s,
  summon 25 s. Before the script a round was 3-4 minutes.
- **Every wait on the client is bounded and polls state**: a loop that waits for a request dump
  (`deploy/log/http_log_<id>_*` count going up) or a screen has a hard cap (seconds or
  iterations) and reports "timeout" instead of hanging; no fixed sleeps while the client may sit
  on a dialog, and a tap sent while a dialog fades in is swallowed, so re-tap after a screenshot
  check. (Two sessions were lost to an unbounded `until` loop.)

## Android client: findings (2026-10-30, session 2)

APK: `brave-frontier-2-19-6-0.apk`, package `sg.gumi.bravefrontier`, versionCode 11014182.

**Manifest** (decoded with `pyaxmlparser`): `minSdkVersion 16`, `targetSdkVersion 30`, no
`usesCleartextTraffic`, no `networkSecurityConfig`, no `extractNativeLibs` (defaults to true,
libs may stay compressed). Consequences: a re-signed APK needs **v1 + v2** signatures
(v1 for API < 24, v2 because targetSdk >= 30); plain `http://` is blocked for the Java/WebView
side unless `android:usesCleartextTraffic="true"` is added (native libcurl is not affected).
Original signing: v1 + v2 only (no v3), 1024-bit RSA cert `CN=gumi Asia`. `resources.arsc`
is stored uncompressed (must stay so and 4-byte aligned).

**libgame.so** (Cocos2d-x v2, libcurl; symbols are NOT stripped, which made this easy).
File offset == virtual address for every site below. Analysis tooling: `pyelftools` +
`capstone` (pip). The client's URL flow:

- `ServiceRequestManager::serviceRequest` sends `GET <HOST 3>/dls` where HOST 3 is set in
  `GumiLiveManager::init` as `"https://" + SERVICE_PHP_URL()`.
  `SERVICE_PHP_URL()` returns the C string `api-sl.bfww.gumi.sg` (arm64 `0x232711f`,
  armv7 `0x1861ed2`), `SERVICE_PHP_URL2()` returns `api-prod-player.bfww.gumi.sg`
  (`0x2327133` / `0x1861ee6`, HOST 4, player API). Both are `std::string(const char*)`,
  so a shorter NUL-padded replacement is fine.
- `ServiceRequestManager::reinitEndPoints` parses the DLS JSON (`game`, `gumilive`,
  `resource`, `mstv`, `bgimage`, `force`, `force_msg`) and rebuilds `NetworkManager`'s
  host table (`this+0x30 + HOST*0x18` on arm64, `+0x20 + HOST*0xc` on armv7) as
  `"https://" + value`. HOST 2 (= scheme + `game`) is used by `featureCheck.php`,
  `getServerTime.php`, `languageCheck.php`, `maintenanceClient.php`, Daily_login.
  HOST 1 gets `":80"` appended unless the game value already contains `:80`
  (would produce `host:9960:80`; who uses HOST 1 is unknown).
- GME action URLs come from `get_PhpReid()` / `PHP_URL()` =
  `"http://" + getServiceEndpoint("game", default "ios.bfww.gumi.sg") + "/" ...`, i.e. the
  game server is already plain http with the host taken from the DLS reply.
  `CommonUtils::getPhpUrl` and friends use a `"https://"` literal (arm64 `0x2369ce3`,
  armv7 `0x1892d2d`).
- `NetworkManager` ctor / `sharedInstance` initialise HOST 0 with
  `"https://api-sl.gl.gumi.sg/"` (arm64 `0x2319e64`, armv7 `0x185b6c3`) using
  `assign(ptr, 26)` with a hard-coded length (`mov w2,#0x1a` at arm64 `0xf5c0d4`,
  `mov w6,#0x1a` at `0xf5c278`; armv7 ARM-mode `mov r2,#0x1a` at `0xccb920`, `0xccbaac`).
  This default is overwritten by the DLS `gumilive` value.
- arm64 is heavily inlined: `"https://"` is also built with `mov/movk` immediates in
  `GumiLiveManager::init+0x90` (`0xf5fbe4..0xf5fbf4`) and `reinitEndPoints+0x3c`
  (`0x1c7a5c8..0x1c7a5e0`); the `PHP_URL` default is a 16-byte copy with an SSO size
  byte (`mov w8,#0x20` at `0x1162cb0`, `0x1162e10`); the `:80` append is
  `append(ptr, 3)` with `mov w2,#3` at `0x1c7a918`. armv7 is plain: it uses the string
  literals everywhere, and the `:80` decision is a `cbz r4` at `0x1383f90`.
- `BFCertificateProvider` installs a curl `CURLOPT_SSL_CTX_FUNCTION` (certificate pinning)
  but it only matters for https, so switching every scheme to http sidesteps it.
- `SaveData::set/getServiceRequestEndpoint` persists an endpoint from `UserInfo`
  (`UserInfo::setServiceRequestEndpointParam`); a fresh install has none.
- `CCUserDefault::getStringForKey` in `GumiLiveManager::init` can override the scheme
  prefix if a saved value exists (empty on a fresh install).
- Java side: `ch.boye.httpclientandroidlib` and OkHttp are present (ads/analytics);
  `android.security.NetworkSecurityPolicy` is referenced, so the cleartext flag matters
  for those paths.

**Patch recipe that follows from this** (both ABIs): swap the two API host C strings for
`host:port`, turn the `https://` literals/immediates into `http://`, fix the two
hard-coded lengths of the gumi-live default (or make the replacement exactly 26 chars),
neutralise the `:80` append, add `usesCleartextTraffic`, re-zip with `resources.arsc`
aligned, sign v1+v2. With port 9960 the replacement must stay <= 19 chars
(`192.168.1.14:9960` is 17, OK).

**Tooling (installed 2026-10-30):** Microsoft OpenJDK 17 (`C:\Program Files\Microsoft\jdk-17.0.21.21-hotspot`,
winget), Android platform-tools (`adb`, winget `Google.PlatformTools`, lands in
`%LOCALAPPDATA%\Microsoft\WinGet\Packages\Google.PlatformTools_...\platform-tools`), Android
SDK at `D:\.Code\android-sdk` with `cmdline-tools/latest` and `build-tools/34.0.0`
(`zipalign`, `apksigner`). Python packages: `cryptography`, `pyaxmlparser`, `capstone`, `pyelftools`.

**Patch script:** `tools/patch_apk.py` applies the byte patches above to both `libgame.so`,
re-zips (signature files dropped, compression kept per entry), then runs `zipalign` and
`apksigner` (v1+v2, self-signed key `build/bfanew-debug.keystore`, created on first run).
It checks the input SHA-256 and every original byte before patching. The manifest is NOT
changed (no `usesCleartextTraffic`): libcurl ignores the cleartext policy; revisit if the
WebView pages (news/notice) matter.

```powershell
$env:JAVA_HOME = "C:\Program Files\Microsoft\jdk-17.0.21.21-hotspot"; $env:Path = "$env:JAVA_HOME\bin;$env:Path"
cd D:\.Code\bravefrontier\bfanew
python tools/patch_apk.py --apk ..\brave-frontier-2-19-6-0.apk --host 192.168.1.14 --sdk D:\.Code\android-sdk --out ..\build
# -> ..\build\bf-192.168.1.14-9960.apk  (verified: v1 true, v2 true)
adb install -r ..\build\bf-192.168.1.14-9960.apk
```

**Revive's build, for reference** (pulled from the Ayn Thor on 2026-10-30 into
`D:\.Code\bravefrontier\revive\revive-base.apk`, SHA-256 `2571a37a...753f1`, signed
`CN=BF Local Server`, arm64 only, manifest and classes.dex byte-identical to the original).
Diff of its `libgame.so` vs the original (60 regions, `scratchpad rvdiff.py`):
- Networking: they **kept https** and swapped the pinned CA. The PEM at rodata `0x2318f4e`
  (1414 bytes, Go Daddy Class 2) is replaced by their own EC "BF Revive Root CA";
  `api-sl.bfww.gumi.sg` -> `api.bfrevive.net`, the S3 CDN string
  `bravefrontier-dlc-130.s3-website-us-west-1.amazonaws.com` -> `cdn.bfrevive.net`;
  `api-prod-player` untouched; `CommonUtils::getPurchasePhpUrl` switched from `http://`
  to `https://`. So if we ever want TLS instead of plain http, replacing that PEM with
  our CA is the known-good route.
- Everything else is gameplay/UI: `PTRateThisAppPopup::showPopupIfRequired` -> `ret`,
  Facebook login frame removed in `OptionTopScene`, credits button -> "reset" button,
  unit fusion slots 5 -> 10 (`UnitMixMainScene`, `unit_mix_mn10.csv`), notice page,
  `MissionResultScene`/`ReinforcementInfoList` hooks, several code caves with new code
  (`0xe63080`, `0x12db460`, `0x16328e0`, `0x186b4e0`, ...). None of it is needed to log in.

**Device:** the user's **Ayn Thor** (Android 13, arm64) still has Revive's build (same
package name, other key, so ours cannot be installed over it without uninstalling).
For testing the user installed **BlueStacks** on the PC instead; it runs the arm64 APK
through ARM translation and reports itself as `SM-S928B`, Android 16 to the server.
Firewall rule for TCP 9960 inbound was added on 2026-10-30.

**Runs against our server (2026-10-30, BlueStacks, forked server):** the client now gets to the
**home screen**. Request order seen in `server/deploy/log` (one `http_log_<id>_<time>.log` per
request, decrypted body + response):
`GET /dls` -> wallpaper -> `GET /accounts/guest/login/` -> `MfZyu1q9` Initialize (7.6 MB reply)
-> `cTZ3W2JG` UserInfo -> `2o4axPIC` FriendGet -> `nJ3A7qFp` BadgeInfo -> `uYF93Mhc`
ControlCenterEnter -> `ynB7X5P9` UpdateInfoLight -> `NiYWKdzs` HomeInfo (x3, stub `{}` is
accepted) -> `Zw3WIoWu` ChallengeArenaResetInfo. The home screen renders fully (party, Vortex Gate /
Light Featured Summon banners, bottom menu) after two startup popups ("Today's Tasks" ->
Close, "Brave Burst 2022 Login Campaign" -> Claim). Opening the **Summon** tab sends
`Uo86DcRh` **GatchaList**, now implemented. **The Summon scene shows the two launch
banners** (Honor Summon with the April 2014 art, Rare Summon with the six starter heroes),
swipe left/right between them. What it took, and what was learned about the summon data:

- `features.json` `gacha_category` **1 -> 0**: with 1 the client uses the 2018+
  `SummonsCategorizationScene` (needs `F_GACHA_CATEGORY_MST` categories matching
  `gacha_info` group ids; showed an empty list) and calls GatchaList; with 0 it uses the
  original flat `SummonsTopScene` and does not call GatchaList at all. 0 is the phase-1 look.
- The flat scene lists `gacha_info` (UserInfo `1IR86sAv`) ids but renders them from
  `GachaMst` (`gacha.json`, Initialize `5Y4GJeo3`): `bg_img`, `btn_img`, dates, caption.
  `SummonsTopScene::getGachaInfoList` **skips type 20000 outside the tutorial** (it is the
  tutorial-only summon; the 2022 "Rare Summon" 2000 had that type), so Rare Summon got
  type 2 (regular gem summon; 1 = honor). Type 20000 should come back as a separate id
  when the tutorial is implemented.
- **The client caches every MST locally and only re-reads it when its version in
  `version_info.json` changes** (`M_GACHA_MST` 952 -> 954 now). Editing `gacha.json`
  without a bump shows nothing new; `adb shell pm clear sg.gumi.bravefrontier` is the
  brute-force alternative (then the first-run flow replays: Terms, phone-permission notice,
  147-file MST download).
- Launch-era art in the archive: `gacha_normal_bg_img_20140425.png` (Honor Summon, Vargas +
  mascots, oldest dated honor art), `gacha_rare_bg_img.png` (undated = original Rare Summon,
  also the client's hard-coded fallback in `SummonsScrlItem::addObject`), buttons
  `gacha_normal_summons_btn1.png` / `gacha_rare_summons_btn1.png`, doors
  `gacha_normal_door.png` / `gacha_rare_door.png`. The undated `gacha_normal_bg_img.png` is
  the 2017 "Super Honor Summon" art, and `gacha_banner_1000/1001/2000.png` are JP placeholders.
- Captions: the Rare Summon shows `caption_msg` from `gacha.json` verbatim; the Honor
  Summon ignores it and shows a localized text ("[SUPER Honor Summon!] ...") from the
  `sgtext` tables (`content/sgtext/sgtext_summonData_9.zip`, `sgtext_summonTypes_9.zip`:
  zips of a base64-encoded `key^text` CSV; `MST_SUMMONDATA_<id>_SHORT/LONGDESCRIPTION`).
  Changing those means re-zipping with a bumped suffix; not done yet.
- `deploy/system` now holds only the two phase-1 banners (the four 2018-2022 ones were
  removed from `gacha_info.json`; `gacha.json` keeps all 1,305 rows, only 1000/2000 edited).
  The proper `phases/NN/` folder mechanism is still to do (step 2).

**Summoning works (2026-10-30):** `F7JvPk5H` **GachaAction** is implemented
(`server/gimuserver/gme/GachaAction.cpp`). Honor Summon -> door animation -> "ACQUIRED!!"
-> unit detail (Fencer Vargas 2*, stats/LS/BB from our reply) -> back to the Summon scene
with the honor points decremented. How the client consumes it (all from the disassembly):
- Request: `1IR86sAv` group with `7Ffmi96v` gacha id, `a329kbl8` request count,
  `324b023k` ticket flag, all **strings**. Encode key `bL9fipzaSy7xN2w1`.
- Response (classic path of `GachaActionScene`): `Km35HAXv` **OpeUserUnit** list
  (`edy7fq3L` user_unit_id -> `UserState::setSelectUserUnitID`, `g30VnzQh` change-rate info,
  `u0vkt9yH` GachaEffectMst id -> door animation), `4ceMWH6k` **the whole unit box**
  (`UserUnitInfoResponse::readParam` calls `UserUnitInfoList::removeAllObjects` first, and
  `GachaActionScene::updateEvent` then looks the new unit up in that list: a missing unit is
  a null deref), `fEi17cnx` updated team info. `UserState::setGachaEffectID` picks the
  `gacha_effects.json` row (rarity -> gWait*/gOpen* files).
- Do **not** send `sdu3jvhH` (`UserUnitResummonInfoSg`): `checkConnectResult` only uses it
  when a global "resummon" flag is set; otherwise `updateEvent` sees the list and enters the
  resummon flow with an uninitialised `ResummonGachaObjectManager` and the process dies.
- Every `UserTeamInfo` must carry `TwqMChon` (reinforcement deck) as `"0,0,0"`: an empty
  string there crashes `UserTeamInfoResponse::readParam` in `CommonUtils::StrToInt`
  (Houdini gives readable backtraces for these: `adb logcat | grep houdini`).
- `StubPlayer.hpp` now holds the one hard-coded player (unit box, wallet) shared by UserInfo
  and GachaAction, and `ServerConfig` is finally filled from `config.json` (upstream never
  did; `initial_free_gems`, new `initial_friend_points`).
- Multi Summon (5x) works too (`multisummon` stays 1). The self-kill seen once
  (`StoreController: opening store` -> `startBillingService()` -> `SIG 9`) is the **Shop
  tab**: the Java SOOMLA `StoreController` opens Google Play billing when the gem shop
  opens, and on BlueStacks with no Google account signed in `BillingClient` reports
  "Billing service unavailable on device." and the app calls `killProcess`. There is no
  feature flag for the shop (`freepaid_gems`, `shop_friend`, `slot`, `video_ads` gate other
  things). Workarounds: sign in to Google Play inside BlueStacks (the sign-in prompt at
  launch), or don't open the Shop tab there; a real device with Play services won't die.
  Shop requests (`BuyCoinStart`, `BuyCoin`) are not implemented anyway.

- `data/response_keys.txt`: the client's full table (489 rows) of top-level JSON key ->
  response class, extracted from `GameResponseParser::getResponseObject`. Use it to know
  which key a handler must answer with.

**Quest flow (2026-10-30):** the home carousel's QUEST page (peeking at the right edge,
`banner_quest`) opens the **world map** (Mistral, `DungeonSelectScene2`), the area plate opens
the **mission list** ("Adventurer's Prairie" / "Start of Adventure", energy 3, 5 battles), the
mission opens the **reinforcement picker** ("Reinforcements could not be found. Proceed?" ->
Yes, no request is sent for helpers yet), then the **squad/items screen** ("Begin Quest"),
which sends `jE6Sp0q4` **MissionStart** (still the upstream "Unsupported" stub, the client
shows the error and stays there). `python tools/dev.py client --to quest|mission` drives to
the map / the mission list. What it took:
- The Quest button first bounced straight back to the home screen (a `HomeInfo` request and
  nothing else): `DungeonSelectScene2::initialize` picks the current area from
  `AreaMstList::getActiveList`, which keeps only areas (and lands) present in
  **`PermitPlaceInfoList`**, and with no area it `changeStepSceneWithFadeBlack(40)`s home.
  That list is UserInfo `yXNM8kL3` (**PermitPlace**, now defined in `net/permit_place.kdl`):
  one record per allowed place with exactly ONE id key (`0Cq2AlXW` gate, `9C64Qwe0` land,
  `VjCY7rX4` area, `MHx05sXt` dungeon, `j28VNcUW` mission; the first key found sets the
  type), plus `qY49LBjw` time limit, `nA95Bdj6`/`5Z1LNoyH`/`5ry19G4Y` quest bonus,
  `s2gM3deu` period text, `C1vG0iKh` enterable flag (`isLockDungeon`). `StubPlayer::
  FillQuestProgress` permits gate 1, land 1, area 100, dungeons 1 (tutorial) and 10,
  missions 1, 2, 10, and marks missions 1 and 2 cleared in `UT1SVg59` (UserClearMissionInfo,
  also defined now: `h7eY3sAK` user, `j28VNcUW` mission, `JATWnN57` clear date, `HIQoGb10`
  count, `Wi4EFe6f` best time, `RWX38boG`/`bGx5ku0F` bc/hc). Ids come from the 2022
  `F_GATE/LAND/AREA/DUNGEON/MISSION_MST` (downloaded MSTs, decoded copies in
  `mst-decoded.zip`): dungeon 10 has missions 10/11/12 and mission 10 requires mission 2.
- "You currently don't have enough energy": `UserTeamInfo` max/current action points were 0;
  `FillTeam` now takes energy (15), arena orbs (3) and friend slots from the `user_level.json`
  row (`UserLevelMst.action_points`, `.unk`, `.friend_count`).
- `select_dlc_android` (features.json) was tried at 0: no effect on the bounce, so it is back
  to 1. `DungeonSelectScene2::initialize` has a selective-DLC branch (`DLCManager::
  checkIfDLCExistForActiveAreas`, `prepareDLCForActiveAreas`, confirmation text
  `TUTORIAL_NODLC_GETDLC_CONFIRMATION`) that did not trigger here. "Begin Quest" downloads
  ~1,750 files (`/content/item/*`, `/content/trial/*`...), all served or blanked.
- The home carousel does not scroll with `input swipe` on BlueStacks (page dots stay on 1),
  but the partly visible page still takes the tap. The Vortex Gate scene is
  `AreaSPSelectScene`; its top-right button is scene 101800 (`RewardsTopScene`), not Quest.
  Scene ids (`GameScene::getGameScene` jump tables): 40/41 HomeScene2, 61 DungeonSelectScene2,
  82 MissionScene (battle), 112 MissionSelectScene2, 2000 LandScene, 2001 GateScene.

**Quests play end to end (2026-10-30):** Begin Quest -> five battles on auto -> Quest Clear
Reward (zel, karma, XP, level up, energy refilled) -> the town "materials" popup -> home, and the
next mission is permitted. Two handlers and a data pipeline:

- `jE6Sp0q4` **MissionStart** (`gimuserver/gme/MissionStart.cpp`, key `csiVLDKkxEwBfR70`)
  answers with the run info (`9Q1Lq5FS` MissionStartInfo), the serial id (`Kz7qfSs5` MissionNum,
  `k9cxD7Ba` -> `MissionInfo::setMissionSerialID`; empty = `INVALID_MISSION_SERIAL_ID`), the
  wallet (`fEi17cnx`, energy minus the mission) and **the whole battle data**, which is not among
  the downloaded MSTs: `pj41dy9g` BattleGroupMst (one per battle), `75t0sx9z`
  BattleMonsterGroupMst (one per enemy: group, monster id, slot, position, drops), `n64NOpfy`
  parts (multi-part sprites, unused so far), `U0v5IeJo` MonsterMst (stats, 38 keys), `89ausgc4`
  AIMst (rows per ai id) and `m9Ada1PN` MonsterSkillMst (empty so far). All key -> field
  meanings are in `packet-generator/assets/net/mission_start.kdl`, read off the client's
  `*Response::readParam` setters. Formats learned from `MonsterParty::entryMonsters` /
  `MonsterUnit::initialize`: position `x:y` in the **640x440** battle field (the client
  multiplies by 0.5), item drop `percent:itemId:rest` joined with commas, AI term
  `partyConds#actConds` (`0:0:0:0#non:0` = none), AI action `attack@-1,-1,...`; the keys shared
  with F_UNIT_MST (`eyUo6a8c` effect frames `24:123:1`, `6Aou5M9r` damage frames, `CEeqs63b`
  status resists `0:0:0:0:0:0`, `3BpHN6VD` HP bar offset `0,4`) use the unit table's values.
  An enemy with a `pn16CNah` unit id uses that unit's sprites (`GameUtils::getMonsterAnmImage`
  -> `unit_anime_<id>`), so `img_a` / `anm_cgg` stay empty; an empty `F4bQ7r8C` skill id means
  the unit's own BB (`BattleUnit::setUnitSkill`). `LE6JkUp7` fixed drops, unit/treasure drop
  strings are sent empty (safe: `MissionDropInfoList::parse` returns on an empty string).
- **Battle data source:** the community datamine `cheahjs/bravefrontier_data` has
  `missions_parsed/<Mission name>_<YYYY-DD-MM-HH-MM>.json`, **608 captures from Sept/Oct 2014**
  of the live server's mission data (per battle: enemies with stats, element, AI id, act counts,
  drops), plus `missions.json` (ids, xp/zel/karma, energy, `requires` chain), `items.json`,
  `ai.json`. Sparse clone at `D:\.Code\bravefrontier\tools-dl\bfdata\bravefrontier_data`
  (`git sparse-checkout set --no-cone /missions.json /items.json /ai.json "/missions_parsed/*"`,
  from Git Bash with `MSYS_NO_PATHCONV=1`). The parser that made them is
  `Deathmax/bfdb` (`tools-dl/bfdb`, its `parsers/common/ai.py` documents the raw AI format).
  `tools/build_battles.py --datamine ... --mst ..\assets\mst-decoded.zip --out server/deploy/battle`
  turns them into **`server/deploy/battle/<mission_id>.json`** (MissionBattleData, wire keys,
  read by the handler per request, so edits need no restart): it numbers battles and monsters
  itself (`<mission><battle:02><slot>`), lays enemies out on the LEFT of the field, staggered
  top to bottom like the original (`LAYOUTS`; calibrated on BlueStacks: x lands where written,
  y shows ~165 units lower than written, so y 340 is ~40% up the field and y 440 ~60%), fills
  the animation fields from F_UNIT_MST, rebuilds AI rows from `ai.json`, and adds the mission's
  xp/zel/karma and what clearing it unlocks (`unlocks`: missions whose `requires` is this one,
  with their dungeon/area/land from F_MISSION_MST). **Coverage:** 204 missions have a capture
  (199 distinct names), 121 more **borrow** the battles of the nearest captured mission of the
  same dungeon (`borrowed_from` in the file; e.g. mission 12 plays mission 11's enemies); that
  the other 404 of the 696 phase-1 missions are **synthesized** (`tools/synth_battles.py`,
  2026-11-01, see "Battle data synthesis" below), so every phase-1 mission has a battle file.
- **Battle data synthesis (2026-11-01).** `tools/synth_battles.py --datamine ... --mst ... --out
  server/deploy/battle --phase server/deploy/phase.json [--wiki data/wiki] [--force]` writes a
  battle file for every phase mission that has none, marked `"synthesized": "wiki" | "land" |
  "land:<Other land>"`. Sources: **`data/wiki/*.txt`**, the Brave Frontier Global wiki's zone
  pages as raw wikitext (one page per dungeon: open `https://bravefrontierglobal.fandom.com/wiki/
  <Zone_Name>?action=raw` in a browser and save the text, several pages per file under `== Zone
  Name ==` lines; the `{{Quest}}` template gives each quest's bosses as **unit ids** (`boss1..6`,
  `bossNhp`), its capturable units and energy / battle count, `{{Zone:Footer}}` the zone's mobs
  and drops by name); 15 zones are saved (Mistral's Tower / Egor Mountains, Morgan, St. Lamia,
  Encervis = 70 missions), the wiki refuses automated fetches (HTTP 402 for tools, the in-app
  browser reads pages one at a time). Without a page, a mission takes the mobs and bosses that the
  captures of its land show (`land`), and a land with no capture at all (Cordelica) borrows the
  land whose captured missions are nearest in XP (`land:Palmyna`). Stats: the unit's max stats
  (`deploy/units.json`) times a factor fitted on the 4,886 captured enemies, log-linear in the
  mission's XP and separate for mobs and for the last battle's bosses (fit 2026-11-01: mob hp
  0.12·xp^0.42, atk 0.68·xp^0.09, def 0.31·xp^0.03; boss hp 0.11·xp^0.60, atk 0.77·xp^0.06, def
  0.26·xp^0.08; a wiki `bossNhp` wins); the AI, act counts, resists, passives and skills of a unit
  come from its latest capture anywhere, else the captures' most common AI (id 20, "attack
  weakness / hp_max / random"); 2-3 mobs per battle, the bosses (plus a mob when alone) in the
  last one, drops from the zone's list at 3% / 10%, zel and karma split from the mission's totals;
  everything is seeded by the mission id. Dev: `/bfanew/give?user=<id>&mission=<id>&dungeon=<id>
  &area=<id>&land=<id>` permits a mission on a test account (the ids from `build_battles.
  load_mission_places`: 81 -> dungeon 80, area 100, land 1). **Verified** 2026-11-01: mission 81
  "The Skyhigh Tower" (wiki, 7 battles, bosses King Glowy / King Gloomy) plays on BlueStacks from
  the Tower of Mistral list to the clear in 230 s on auto. **Lesson (a client crash):** the first
  try died 2 s after MissionStart (SIGSEGV at 0x48 on the GL thread): the enemies carried skill
  ids such as 1553 / 1253 / 1853 resolved by name from the full F_SKILL_MST, and **the client's
  UnitSkillMstList holds only the brave bursts of the units it has** (the phase-filtered
  F_UNIT_MST), so any other id is a null lookup at mission start. Both builders now take
  `--phase` and drop every skill that is not a brave burst of a phase unit (974 kept, 2,375
  dropped across the captures: the boss-only dialogue and attack skills are gone until a way to
  serve them is found; the enemy then just attacks).
- **Enemy skills (2026-10-31).** The captures list each enemy's skills by English name with
  the hit count, the damage distribution and bfdb's parsed effects, not the raw rows; but the
  client's own `F_SKILL_MST` / `F_SKILL_LEVEL_MST` already hold every enemy skill (unit brave
  bursts and the boss-only ones, dialogue lines included: skill type 5 with Japanese names),
  and `BattleUnit::setUnitSkill(MonsterMst*)` reads them: MonsterMst `F4bQ7r8C` is
  **`skillId:level@skillId:level`** (empty = the unit's own BB), each looked up in
  `UnitSkillMstList` (`BattleUnit::setSkillParam`), so `m9Ada1PN` MonsterSkillMst stays empty.
  `build_battles.py` resolves a captured skill to an id by name (`sgtext_skillName_<v>.zip`,
  `MST_SKILLS_<id>_NAME`), then by hit count, damage distribution (`damageFrames` entries and
  their percent column) and the attack parameters of each level (`activeSkillInfoByLevel`
  `lv:cost:params@...`, proc 1 / 14: index 0 bb atk%, 1 flat atk, 5 bb dmg%, as bfdb's
  skills.py reads them): 3,338 of the 3,349 captured skills resolve, the rest are names the
  2022 text table no longer has ("Soul Desecretion", two of Great Thief Leon's lines). The AI
  terms: the client's `skill:<n>` condition is **1-based** (the n-th entry of the monster's
  list; ai.json says `skill:1` where the capture tool wrote "set skill": <first name>), so
  "set skill": name -> `skill:<index+1>`; `skill_use` / `skill_unuse` stay as they are. The AI
  rows now come from the capture's own 2014 AI when it has one (the 2021 ai.json agrees on
  every id checked, it is the fallback). Two things learned the hard way: (1) the client's
  `UnitSkillMstList` does not hold every F_SKILL_MST row (skill 1050 "Eruption" is in the served
  table, yet `DLCManager::requestUnitFilesForMission` -> `requestMonsterSummonFiles` looked it
  up, got null and crashed at mission start), while the brave bursts of the units in the
  (phase-filtered) F_UNIT_MST are there, so the resolver prefers the enemy unit's own BB, then
  any BB of a unit the client has (`bbId` of F_UNIT_MST), before the frame / parameter scores;
  (2) **the enemy name in battle is `TextManager::getMstText("MST_MONSTERS_<monster id>_NAME")`**
  with the row's `3evIn0zZ` as the fallback, and our generated monster ids
  (`<mission><battle:02><slot>`) collided with the 2022 `sgtext_monsters` table: Cave of Flames'
  Goblins were labelled "Siren", Warlock Liza "Knight Sergio" (and every mission since the
  battle builder exists). `load_monster_name_ids` reads that table and the builder prefixes a
  colliding id with 9 (630 monsters), so the capture's own names show.
- `9TvyNR5H` **MissionEnd** (`gimuserver/gme/MissionEnd.cpp`, key `oINq0rfUFPx5MgmT`), sent
  by `MissionResultScene::initConnect` (then a FriendGet). Request: `Kz7qfSs5` {serial id,
  `j3g5P4cq` state, 2 = cleared}, `rXvA1E5y` battle stats (`Najhr8m6` zel and `HTVh8a65` karma
  picked up, `4T0Q2Bh5` items `id:count,...`, `hoG2ieT5` BC, `6PLsn8xo` HC, `L16VDrk5`
  monsters killed, `Lu0pAk1R` enemies per battle, `TW1Mrtp5` cheat log...), `0xgIw1Ns` per
  squad unit. Reply: `F5Vs19mb` **MissionReward** (`net/mission_end.kdl`: clear bonus, lvup flag,
  before level, inc exp, clear dungeon/area/mission ids -> "new area" via
  `GameUtils::getAppearDungeon`, zel, karma, up types, guild bonuses...), `fEi17cnx` wallet
  (zel/karma/exp added, level from `user_level.json`, a level-up refills energy), `UT1SVg59` the
  cleared mission and `yXNM8kL3` the places it unlocks. The serial id -> mission id pair is
  kept in memory by `GimuServer::rememberRun` (MissionEnd does not send the mission id).
  Still stubbed: first-clear bonus (`92ms7J5M`, format unknown; mission 12 gives a gem), item
  pickups are not added to the warehouse, nothing survives a relogin (no persistence).
- Dev loop: `client --to mission`, then `tapk mission_first`, `tap 886 557` (helper dialog Yes),
  `tap 960 752` (Begin Quest), `tap 1067 479` (AUTO); a mission takes ~2 min on auto.

**Persistence (2026-10-30):** `gimuserver/db/Player.hpp/.cpp` + migration `30102026_PlayerTables`.
- Identity: the client's guest login sends `vid` (a device UUID it keeps), `dn` (device name),
  `ak` (the user id of its last login, empty on a fresh install) and `identifiers`
  (`android_id`). `accounts` maps `vid` -> an 8-digit gumi user id + a random token (the client
  echoes both as `iN7buP2h` / `iN7buP1i`); Initialize creates/loads the player by gumi id and
  answers the 16-digit `user_id` (`h7eY3sAK`), which the client then sends in every request's
  `IKqx1Cn9` login info; `Db::LoadPlayer` looks it up by user id, gumi id as fallback (that is
  how an install that knew the old hard-coded id migrated silently). The token is not verified.
- `players.team` is the `UserTeamInfo` JSON (wallet, level, exp, energy...), `player_units.data`
  the `UserUnitInfo` JSON (ids assigned by the autoincrement, so `user_unit_id` starts at 1);
  decks/clears/permits/runs/items are plain rows. Energy: `energy_at` is when the stored
  value was computed; `Db::RegenEnergy` adds 1 point per `energy_regen_seconds` up to the max
  from the level table and sets `action_rest_timer` (`f0IY4nj8`); MissionStart resets the
  clock when it spends from a full bar; a level-up refills (`NewPlayer::ApplyLevel`).
- Verified on BlueStacks: new account created at first login ("Summoner", 15/15 energy, 2/200
  units after one Honor Summon, still there after a relaunch), mission 10 cleared on auto ->
  level 2, 16/16, zel/karma/honor points updated, mission 11 on the list after a relaunch, the
  pickups in `player_items`.
- To start over: stop the server and delete `server/deploy/gme.sqlite` (the migrations recreate
  it), then `adb shell pm clear sg.gumi.bravefrontier` is NOT needed (the device keeps its `vid`
  and simply gets a new account).
- SQL placeholders: write `$1, $2, $3...` in the order they first appear in the statement.
  SQLite takes `$3` as a *named* parameter numbered by first appearance and Drogon binds the
  arguments positionally, so `count - $3 WHERE user_id = $1` received the user id as `$3`
  (ItemSell credited the zel but never removed the items until `Db::TakeItems` was reordered;
  `scratchpad sqlscan.py` finds such statements).
- The Java "Enjoying Brave Frontier? Please rate" dialog showed up after the second clear
  (`PTRateThisAppPopup`, which Revive patched to `ret`); "Remind me later" dismisses it.

**DeckEdit (2026-10-30):** `m2Ve9PkJ` (key `d7UuQsq8`, `gimuserver/gme/DeckEdit.cpp`,
`net/deck_edit.kdl`) is sent when leaving Manage Squad (`PartyEditTopScene2::touchEnded`), from
the squad list, the player info screen and the options screen (handle name / friend message).
Request: `dX7S2Lc1` every slot of every squad as strings (`U9ABSYEp` deck type 1, `zsiAn9P1`
squad 0-9, `edy7fq3L` user unit id, `gr48vsdJ` 0 leader / 1 member, `XuJL4pc5` slot 0-4) and
`Ti62XfZK` {`Z0Y4RoD7` active squad, `TwqMChon` helper squads "a,b,c", `gKNfIZiA` arena squad,
`qvY4IVL7`/`K8U410zY`/`38cDi7eb` 0, plus `bM7RLu5K` friend message and `B5JQyV8j` handle name
only when edited}. The reply carries the saved squads and the team info; slots pointing at a
unit the player does not own are dropped. Note the request sends deck type / number as strings
while `UserPartyDeckInfo` (the reply) has them as ints, hence the separate `DeckEditEntry`.
Verified: a unit placed in squad 1 is still there after a relaunch (`player_decks`).

**Fusion / UnitMix (2026-10-30):** `Mw08CIg2` (key `JnegC7RrN3FoW8dQ`, `gimuserver/gme/UnitMix.cpp`,
`net/unit_mix.kdl`), sent by `UnitMixConnectScene::initConnect`. Request: `Km35HAXv` list of
{`mnZ5K4Ii` order, `edy7fq3L` user unit id}, order 1 = base, 2.. = materials; `mCE3rUu5`
{`Rs7bCE3t` zel the client expects}; `60subGk3` {`81GjwoWy` 1, `2vnqRIr3` unit count}. Reply:
`1ZbHB6Im` **UnitOpeResult** (before/after level, exp, BB level, `6hMI5xeF` success type 0/1/2,
`5Hi7MVQe` one `lv:hp:atk:def:rec` record per level reached joined with commas, parsed by
`UnitMixPlayScene::parseMixResult` with strtok, so a wrong separator is a null deref), `fEi17cnx`
wallet, `4ceMWH6k` the whole box. The rules were read off the client so the server matches the
numbers the Fuse screen predicts (checked: zel 200 and XP 306 for a lv1 same-element 2* material):
- exp per material (`GameUtils::getMixExp`): material total exp / 2.5 + cost * 2 + adjust exp
  (`unitAdditionalXpBoost`) + 100 / 200 / 500 / 1000 / 1500 / 3000 / 5000 / 10000 by rarity
  1..8, x1.5 when the material has the base's element; Great / Super success multiply by the
  define table rates (`2inP0tCg` 1.5, `zn65EXYF` 2.0; the chances, 10% / 2%, are ours);
- zel (`GameUtils::getMixGold`): sum over materials of (base level + material level) * 100;
- level from the unit's exp pattern (`unit_exp_pattern.json`: `5UvTp7q1` pattern, `D9wXQI2V`
  level, `grKTq15A` total exp), capped at the unit's max level; stats interpolated linearly
  between the level-1 and max-level values (the real curve is not known yet);
- BB level: each material with the base's brave burst (same bb id or same unit family) has a
  20% chance to raise it (max 10).
- **`server/deploy/units.json`** is the server-side unit table (`tools/build_units.py` from
  the decoded F_UNIT_MST: rarity, cost, element, max level, exp pattern, base/lord stats,
  adjust exp, skills, sell price), loaded into `ServerCache::units()`; force-added to git like
  the battle files.
- Units in a squad cannot be materials (the client refuses: "Squad is being edited"), so a
  test needs a freshly summoned unit; `Db::RemoveUnits` also drops the squad slots of consumed
  units. Verified: Selena lv 6 + a lv 1 Magress -> lv 8 (+204 exp, 700 zel), the level-up
  animation and result screen play, the box and wallet persist.

**Selling / UnitSell (2026-10-30):** `Ri3uTq9b` (key `92VqcGFWuPkmT60U`, `gimuserver/gme/UnitSell.cpp`,
`net/unit_sell.kdl`). The client sells locally ("Units sold for N Zel", the box shrinks) and
only sends the request when **leaving** the Sell Units screen (`UnitSellRequest::addResult`
queues them): `60subGk3` {`n7Pmh6ex` count}, `mCE3rUu5` {`Rs7bCE3t` zel}, `Km35HAXv` list of
{`mnZ5K4Ii` order 2.., `edy7fq3L` user unit id}. The server removes the units (and their squad
slots), credits `sell_price` from `units.json` and answers wallet + box. Verified: a Fencer
Vargas sold for 300 zel, gone after relaunch.

**Evolution / UnitEvo (2026-10-30):** `0gUSE84e` (key `biHf01DxcrPou5Qt`, `gimuserver/gme/UnitEvo.cpp`,
`net/unit_evo.kdl`). The client enables Evolve only at the base's max level ("Insufficient Lv"
otherwise) and with the F_UNIT_EVO_MST materials owned. Request: `8Z2NQrx1` list of {`mnZ5K4Ii`
order (1 = base), `29MgiJIQ` type (1 unit, 2 item), `inU8Q4gL` user unit id / item id},
`mCE3rUu5` {`Rs7bCE3t` zel}, `I82p0wCL` {`pn16CNah` target unit id}, `60subGk3` {`q7DwK6Am` 1};
the `Km35HAXv` list of the summoner-unit variant is NOT sent. Reply: `I82p0wCL` **UnitOpeEvo**
(`pn16CNah` new unit id, `edy7fq3L` user unit id, `t9FEW2KC` / `u1ECvfg8` the ids before,
`dV3qji4I` evolution type 0), wallet, box. The server checks the recipe in
**`server/deploy/evos.json`** (`tools/build_units.py --evos`, from the decoded F_UNIT_EVO_MST:
evolved id, zel, karma, materials), requires max level, consumes the unit materials and the
zel, and rewrites the base as the evolved unit at level 1 (same user unit id, BB level and type
kept). Item materials (type 2) are taken from the warehouse (see "Items / warehouse"). Verified: Selena lv 12 +
Water Nymph (20130) -> Glacial Selena (20012) with the evolution animation, "RARE!!" reveal,
2,500 zel.
- Dev route **`GET /bfanew/give?user=<gumi user id or user id>&unit=<unit id>|item=<item id>&count=1&lv=1`**
  (`DevController::HandleGive`) puts units in a box or items in the warehouse, for materials and
  max-level bases (there is no drop source for Nymphs yet). `NewPlayer::UnitFromTable` / `SetUnitLevel` build the
  record from `units.json`.
- Unit **type** (`nBTx56W9` unit_type_id: 1 Lord, 2 Anima, 3 Breaker, 4 Guardian, 5 Oracle) was
  being filled with the rarity by the summon (the sell list said "[Anima] Fencer Vargas"); now
  random 1-5 on summon, 1 for given units, kept through evolution.

**Items / warehouse (2026-10-30):** the warehouse is `player_items` (item id -> count, equipped
items included) and the items equipped for quests `player_equip` (slot -> item id, count; migration
`30102026_PlayerEquip`). `Db::LoadItems / AddItems / TakeItems / LoadEquip / SaveEquip`
(`gimuserver/db/Player.cpp`); `TakeItems` clamps the equipped counts to what is left.
- UserInfo `9wjrh74P` **UserWarehouseInfo** (`net/user.kdl`, from `UserWarehouseInfoResponse::
  readParam`, which clears the client's list first, so every reply carries the whole warehouse):
  `n6E8iMf3` item index (the record id the client echoes when selling; we use the item id),
  `kixHbe54` item id, `wgV86x1q` count, `dJNpLc81` new flag. `71U5wzhI` **UserEquipItemInfo**:
  `kixHbe54` item id, `XuJL4pc5` slot, `wgV86x1q` count (also replaces the client's list).
  MissionEnd and UnitEvo replies carry both lists too.
- `ruoB7bD8` **ItemEdit** (key `DHEfRexCu0q5TAQm`, `gimuserver/gme/ItemEdit.cpp`): Manage Items on
  the quest squad screen. The request is queued (`ConnectRequestList`) when leaving the screen and
  sent with the next connection (it showed up right before MissionStart on Begin Quest): the WHOLE
  equipped list as `71U5wzhI` {`kixHbe54`, `XuJL4pc5`, `wgV86x1q`, all strings} plus `nAligJSQ`
  bonus items (ignored). The server stores it, capped at the warehouse counts, and answers the
  stored list and the warehouse. Verified: 5 Cures (20000) equipped, shown in battle's Items tray,
  still equipped after a relaunch.
- `qDQerU74` **ItemSell** (key `73aFNjPu`, `gimuserver/gme/ItemSell.cpp`): like UnitSell the client
  sells locally and sends `M73i1c5U` {`n6E8iMf3` item index, `wgV86x1q` count} through
  `ItemSellConnectScene`; the server removes the items, credits `sell_price` from
  **`server/deploy/items.json`** (`tools/build_units.py --items`, from the decoded F_ITEM_MST: name,
  type 1 consumable / 2 material / 3 sphere, rarity, sell price, max stack, max equipped) and
  answers wallet + warehouse. Verified on the village's Sell Items screen (Green Grass x9 -> "Items
  sold for 27 Zel", the request arrives with the next connection, the row is gone, zel +27); the
  first try credited the zel without removing the items because of the `$N` placeholder order in
  `Db::TakeItems` (see "SQL placeholders" under "Persistence").
- MissionEnd request `47cJBUxz` = items used during the battles, `itemId:count,...`
  (`BattleUseItemLogList::getCsv`; the key is absent when nothing was used): removed from the
  warehouse and the equipped counts. Not exercised either: the Cure cannot be used at full HP and
  mission 10's enemies never hit back (do it on a harder mission).
- UnitEvo item materials (`29MgiJIQ` type 2, `inU8Q4gL` item id) are now checked against the
  warehouse and consumed (`itemId:1` per material).
- `BjAt1D6b` **DungeonEventUpdate** (key `k5EiNe9x`, URL `/actionSymbol/4iqcLm1s.php`,
  `gimuserver/gme/DungeonEventUpdate.cpp`): the client reports a story event as watched (area /
  dungeon / mission event scenes, the town's first visit with Karl); the body is the login info with
  `9yVsu21R` the scenario id (`randall,`) and `N4XVE1uA` `|0,0`. Stub answering `{}`; the watched
  events are not stored (UserScenarioInfo is still a mock), so the dialogues replay.
- Town: `UserTownLocationDetail` (`s8TCo2MS`: `un80kW9Y` location id, `qA7M9EjP` start date,
  `mDaE3t6A` tap count, `eip3MS5L` drop item info) is now sent for every `town_location.json` row:
  `MyTownTopScene::downloadFiles` dereferences the record of every location whose `need_mission_id`
  is cleared (mission 11 -> location 3 crashed the Town tab in `getTapCnt` once mission 11 was
  cleared).
- **Village crash (solved 2026-10-30):** after that fix the village still died on BlueStacks when
  its scene started (Town tab = scene 900 `MyTownTopScene`, or the "Village" button of the Randall
  map = scene 1300 `RandallTownScene`): `scudo ERROR: invalid chunk state when deallocating` in the
  game process right after the BGM switch, then a main-thread SIGSEGV with pc 0, no backtrace, the
  same fault address every time. Not the data: blanking the warehouse / equip / clears / permits
  lists in UserInfo (dev toggle `BFANEW_BLANK=warehouse,equip,town,clears,permits,units,decks`,
  `gimuserver/gme/UserInfo.cpp`) changed nothing, and the scene reads only location id, tap count
  and drop item info from the town records (all handled). The cause was the **content archive**:
  `content/sam/MapVillage/MapVillage.sam` is the 2.19.6 animation but the archive carries an older
  image set (`MapVillage_320x200.png`, `..._640x208_2.png`) instead of the two images the .sam
  references (`MapVillage_320x289.jpg`, `MapVillage_640x384.png`, both in the APK's `assets/`); of
  4,296 .sam files served, this is the only one with that gap (`tools/fill_content_from_apk.py`
  scans them). The client's bulk download (first-launch DLC, or Settings > Downloadable Content)
  fetched them, got the blank 1x1 PNG we answer for missing images (as `image/png` for a `.jpg`
  path), cached them, and `SuperAnimList::addAnime` corrupted the heap on every visit. Fixes:
  `tools/fill_content_from_apk.py` extracts APK assets that .sam files reference but the archive
  lacks into **`deploy/apk_assets/`** (flat, gitignored) and `BfWebController` serves a missing
  `/content/...` file from there by basename; missing `.jpg` paths get a real JPEG
  (`apk_assets/_blank.jpg`, the archive's smallest); `.DS_Store` / `Thumbs.db` list entries get an
  empty 200 (a 404 stalled "Full Download" for good at 10%); `deploy/log/dlc_404.log` now appends
  across restarts with one path per line. Verified: fresh client (data cleared; the account came
  back by itself, the device id is the account key) -> Town tab opens the village (Event Bazaar,
  Synthesis, Upgrades, Items, Randall icon). Then Settings > Downloadable Content > Full Download (every file
  re-fetched from this server, 20 min) -> Town tab opens the village again with the downloaded
  files in place.
  Recovery for an installed client with the bad cache: Settings > Downloadable Content > Full
  Download (re-fetches everything, ~40 min at the client's one-file-per-second pace), or clear the
  app data (`python tools/dev.py client --fresh`, hands-off since 2026-10-31, see "Dev loop").
- `rCB7ZI8x` **UpdateEventInfo** (key `L1o4eGbi`, URL `/actionSymbol/UEt5bqp4.php`,
  `gimuserver/gme/UpdateEventInfo.cpp`): `HomeScene2::updateEvent` sends it (async, login info +
  signal key) once the player has sat on the home screen for more than 300 s
  (`UpdateEventInfo::isNeedUpdate`). It was unregistered: "Unsupported request: rCB7ZI8x" popped
  up after the 20-minute download and the client restarted on OK. Stub answering `{}`.
- `4P5GELTF` **ItemMix** (key `AFqKIJ8Z4mHPB9xg`, URL `/actionSymbol/cmk86pt4.php`,
  `gimuserver/gme/ItemMix.cpp`, **verified 2026-10-30**): item synthesis on the village's
  Synthesis screen. `MyTownItemMixScene::mixItem` does everything locally (karma -= recipe karma,
  materials taken, result added, `ItemMixResultInfoList` counts the mixes per recipe) and one
  request goes out with the next connection: group `JTf2jY5o` with a single node
  `{4HqhTf3a: "recipeId:count,..."}` (`ItemMixRequest::itemCsvData`). The server replays the mixes
  against the stored warehouse (materials present, key items owned but not consumed, karma) and
  answers `fEi17cnx` + the whole warehouse. Recipes are F_RECIPE_MST (`system/receipes.json`,
  Initialize `8f0bCciN`): `4HqhTf3a` id, `kixHbe54` result item, `wgV86x1q` result count,
  `HTVh8a65` karma, `75YV2q1i` materials `itemId:count,...`, `7dEB1Kwj` key item ids,
  `H6k1LIxC` mix limit (-1 unlimited, 75 event recipes are limited), `XuJL4pc5` display order,
  `qA7M9EjP` / `SzV0Nps7` validity. Verified: Cure (recipe 1009 = Green Grass + Green Drop) twice ->
  request `1009:2`, warehouse 10000 3->1, 10300 9->7, 20000 5->7.
- **Synthesis List = permitted recipes.** `MyTownItemListScene::setRecipeList` lists exactly the
  UserInfo `51yQrDBR` **PermitReceipe** records (`4HqhTf3a` recipe id, `H6k1LIxC` creations done,
  `VDKB0Y5h` seconds left or -1; a 0 there reads as expired and hides the recipe, which cost an
  hour): with an empty list the screen is blank. UserInfo now sends, for every town facility
  (`Lh1I3dGo` TownFacilityMst) whose `need_mission_id` is 0 or cleared, a level-1
  `UserTownFacilityInfo` and the recipes its level releases (`d0EkJ4TB` TownFacilityLvMst
  `rGoJ6Ty9` release_receipe): facility 2 lv 1 = 29 item recipes including Cure, facility 1
  (mission 21) lv 1 = 357 sphere recipes. Facility upgrades: see TownFacilityUpdate below;
  creations done are not tracked.
- `8v43tz7g` **TownFacilityUpdate** (key `rq7Yd1nG`, URL `/actionSymbol/kba85fVc.php`,
  `gimuserver/gme/TownFacilityUpdate.cpp`, **verified 2026-10-31**): the village's Upgrades
  screen (`MyTownFacilityExtScene2`, "Upgrade Facility"). Everything is local: each tap on the orb
  (hold for a stream) injects 1 karma into the facility or location in the centre
  (`injectionFacility`: `UserTownFacilityInfo::karma` accumulates toward `TownFacilityLvMst`
  karma of the next level, then lv++ and karma resets; locations likewise with
  `TownLocationLvMst`), the team karma drops, `UserState::addInjectionKarma` counts it, and the
  request goes out with the next connection: group `EuY6L7AX` {`HTVh8a65` karma injected},
  `YRgx49WG` the whole facility list and `yj46Q2xw` the whole location list as the client now has
  them (`y9ET7Aub` / `un80kW9Y` id, `D9wXQI2V` lv, `HTVh8a65` karma toward the next level). The
  server recomputes the cost of each change from the level tables (a jump it cannot price is
  refused and the stored values win), takes the karma, stores the levels in **`player_town`**
  (migration `31102026_PlayerTown`, kind 1 facility / 2 location, `Db::LoadTown / SaveTown`) and
  answers `fEi17cnx`, both lists and `51yQrDBR`. `gimuserver/gme/TownState.hpp` builds those
  three lists for UserInfo and this reply: facilities and locations whose `need_mission_id` is 0
  or cleared, stored level (1 by default), and the recipes released by every facility level up
  to the current one. Facility 2 (Synthesis) has 31 levels (100 karma to lv 2, 250,000 to 31),
  facility 1 (spheres, mission 21) 79; facilities 3-6 have a single lv-0 row (no upgrades).
  Locations (Forest, Mountain...) have up to 8 levels (500 karma for lv 2) and change their
  collection drops (`8fjZN6Mx`, taps, zel rate): TownState rolls a location's collection from
  the row of its stored level (see TownUpdate).
  Verified: Synthesis lv 1 -> 2 for 100 karma -> request `EuY6L7AX` 100, facility 2 lv 2,
  stored `(1, 2, 2, 0)`, reply 33 recipes, the Synthesis List gains Lunar Treat / Mooncake /
  Crescent Dew / Antidote.
- `CuQ5oB8U` **TownUpdate** (key `w1eo2ZDJ`, URL `/actionSymbol/uVrph04Y.php`,
  `gimuserver/gme/TownUpdate.cpp`, **verified 2026-10-31**): the village's item collection, the
  sparkling locations (`eff_page_<effect_type>` particles on every location whose `s8TCo2MS`
  record has `mDaE3t6A` tap_cnt >= 1). `MyTownTopScene::collectItemEff` is local: a tap takes
  entry `[n - tap_cnt]` of the record's `eip3MS5L` drop_item_info, which is
  **`itemId:zel:karma,...`** (one entry per tap, n entries, item `0` = none; the earlier
  `itemId:count` reading was wrong), adds the item (1) to the warehouse (`WAREHOUSE_OVER_ERROR`
  when full), the zel and karma to the wallet, `decTapCnt(1)`, logs the tap in
  `MyTownCollectLogList` and queues `TownUpdateRequest` for the next connection: group
  `EuY6L7AX`, one node, `0mRaAo39` = `"locationId:tapCnt,..."` (taps since the last report) and
  `LzKDI2i7` = the sound shop purchases (ignored). The server pre-rolls each location's
  collection (`gimuserver/gme/TownState.hpp`: tap count in `[Q43bK0zN, dDYX75JR]`, the item from
  the weighted `8fjZN6Mx` pool `itemId:weight,...` of the `TownLocationLvMst` row of the stored
  level, `NC1Lq7b4` % chance of `[U0Y8phRm, TV7SB9rh]` zel, `PKa13dH7` % of
  `[u8tsAe9F, GRFYaC71]` karma), stores it in **`player_town_collect`** (migration
  `31102026_PlayerTownCollect`: tap_cnt, drops, refill_at; `Db::LoadCollect / SaveCollect`) and
  sends it in UserInfo. On TownUpdate it grants the next entries of its stored copy (never more
  than the taps it had), arms the refill timer when a location is empty (`kRefillSeconds` =
  1 hour; the original interval is unknown, the client never reads `qA7M9EjP` startdate; a
  location refills only once every tap was reported, so unreported taps are never doubled) and
  answers `fEi17cnx`, `9wjrh74P` and the whole `s8TCo2MS` list (`GameResponseParser` dispatches
  it to `UserTownLocationDetailResponse`, which replaces the list). Verified: fresh login rolled
  4 / 9 / 6 / 8 taps on locations 1-4, all four sparkle; two taps on location 2 (Forest,
  `10300:0:12,10300:106:10,...`) -> request `0mRaAo39` `"2:2"`, stored taps 9 -> 7, item 10300
  9 -> 11, +106 zel, +22 karma; one more -> `"2:1"`, 7 -> 6, 10300 12, +4 karma, the home
  header shows the same wallet. Tap positions: `TownLocationMst` `SnNtTh51`/`M6C1aXfR` are the
  top-left corner in 640x960 design units, y from the top (location 2 centre = window px
  1076,625 with the current BlueStacks layout). Drop items 10400-10402 / 10500-10502 /
  10700-10702 are the colour Claws / Fangs / Hides, 10600-10605 the God Eyes, 11000-11004 the
  stones; the Forest (2) gives Green Grass 10300 at level 1.
- Town records on the wire (from `MyTownTopScene`): `qA7M9EjP` startdate is never read by the
  client. Town locations are "open" when `HSRhkf70` need_mission_id is `0` or cleared.
- Where things are in the client: the Units tab has no item menu; the home MENU has only the
  guides; item screens (`ItemTopScene`, scene reached from `GameScene::getGameScene`) are in the
  village. The Randall facility map is `RandallTopScene` (home icon next to MENU).

**Free economy (2026-10-30, design rule):** nobody pays for anything on this server. Energy
refills, unit-slot and warehouse extensions, continues and friend slots cost **0 gems**, gem
summons cost **1 gem**, and every first mission clear gives at least one gem.
- The client's gem balance is **`brave_coin`** (`03UGMHxF` in `UserTeamInfo`): that is what the
  header shows, what the summon dialog compares ("Available: N Gem(s)") and what ShopUse / the
  gacha tables price in. `free_gems` / `paid_gems` (`92uj7oXB` / `d37CaiX1`) are only the
  free-vs-paid split; the server keeps them in step but never shows them. GachaAction now pays
  from `brave_coin` (`kGemCost` 1).
- Prices live in tables the client caches by version (`python tools/dev.py bump <MST>` after an
  edit, the client re-reads at the next launch): `system/defines.json` (`DefineMst`, M_DEFINE_MST)
  `C8KkHGa7` ActionPointHealCnt (energy refill gems), `sy9G24Su` FightPointHealCnt (arena),
  `I73XkAQi` UnitBoxExtCnt, `CA01vo2Q` ItemBoxExtCnt, `QW3HiNv8` ContinuDiaCnt, `3Ep5akHJ`
  FriendExtCnt, all 0 now; `eRQvzLeF` ActionPointThreshold (plain int) **had to be added**: the
  energy "+" refuses with ENERGY_RECOVER_GEM_MAX_THRESHOLD when energy >= threshold, and a missing
  key reads as 0 (9999 now); `hAiXsSPF` ActionPointRecoverFixed (0, the server refills to max
  anyway). `system/gacha.json` (`GachaMst`, M_GACHA_MST) `03UGMHxF` gem price, 5 -> 1 on every
  type-2 (gem) summon, "5 Gems" texts rewritten; **the summon dialog's price comes from
  `system/gacha_info.json`** (UserInfo `1IR86sAv`, `GachaInfo::getNeedBraveCoin`, `03UGMHxF`),
  1 there too (sent at login, no version bump).
- `xe8tiSf4` **ShopUse** (key `qthMXTQSkz3KfH9R`, URL `/actionSymbol/T10fSAQ6.php`,
  `gimuserver/gme/ShopUse.cpp`): everything the client pays gems for outside summons. The scenes
  fill `UserState` (setShopUseType, setBravecoin = the define price, setExtCnt) and
  `ShopUseRequest::createBody` sends one node of group `32ibWjFG`: `60IsqxDt` type, `03UGMHxF`
  gems, `5gXxT7LZ` ext count, `rA9jDCP5`, `edy7fq3L` unit id, `T1IxCpTF`, `9i2xhMaJ`; group
  `d2b8d79J` {`n37dK9Xi`}. Types: 1 energy refill (home "+" = `GameScene::confirmAnswerYesEnergy`,
  `MissionSelectShortageStaminaScene`), 10 the same from the result's repeat prompt, 2 unit box
  +ext (`ShopUnitBoxExtScene`), 3 warehouse +ext (`ShopItemBoxExtScene`), 7 friend slots +5,
  5 continue after a defeat, 4 arena points, 6 stamp, 8 colosseum, 9 unit info reset, 11 / 12
  summoner shop. The server never deducts gems; 1 / 10 set the energy to max, 2 / 3 grow
  `max_unit_count` / `warehouse_count` (caps 4000 / 3200), the rest is accepted and ignored.
  Reply: `fEi17cnx`. **Verified**: energy 11/17 -> "+" -> prompt -> Yes -> request type 1 with
  0 gems -> 17/17, balance unchanged. Box extensions are unverified: their scenes sit in the
  Shop tab, which kills the client on BlueStacks (SOOMLA billing, see "Multi Summon"); that
  same `StoreController.startBillingService()` also killed the client at two launches tonight.
- **First-clear rewards** (`MissionEnd`, **complete 2026-10-31**): `tools/build_battles.py`
  exports F_MISSION_MST `missionRewards` as `first_clear` (`"type:id:count"`) into
  `deploy/battle/<id>.json`; on a first clear the server grants them plus **1 gem for every
  mission**. The types, read off `MissionResultBaseScene`: **8 gems** (`getRewardBraveCoin`),
  **3 zel** (`getRewardBonusZel`), **11 karma** (`getRewardBonusKaruma`), 17 SP
  (`getRewardBonusSp`, 2016, ignored), **6 a unit**, **7 a sphere**, **4 an item** (the earlier
  "2 zel / 3 karma" reading was wrong: 2 never occurs). Gems / zel / karma go back in `92ms7J5M`
  as `type:id:count,...` (three fields per entry, the amount is the third; `"8:1"` is silently
  dropped) for the result banner. A unit is built from `units.json` (level 1, phase-checked),
  added to the box and announced in `3MAT6quo` as `<unitId>:<userUnitId>:1:1`
  (`MissionResultUnitScene`'s constructor keeps entries of 4+ `:` fields whose **4th** is `1`,
  looks the unit up in `UnitMstList` by the **first** and, when the 3rd is not 0, shows a "?"
  frame that reveals the unit; the first try, `1:<unitId>:...`, crashed the "Units Acquired"
  page in `updateEvent` on the missing frame sprite, a null `runAction`); spheres and items go
  to the warehouse (the result screens do not show them). The reply now always carries the whole box (`4ceMWH6k`): an empty list
  there would wipe the client's. Phase 1 has unit rewards on the Vortex "Heroes" Lv.4 dungeons
  (Burst Frog 10312), Metal Ghost 60132 on 100600, the GGC spheres (37600...) with 1,000,000 zel
  / 200,000 karma on the Lv.3 chronicles. Dev: `/bfanew/give?user=<user id>&level=N` puts a
  player at a level with a full bar (expensive missions). Verified by `gme.py`: 100803 ->
  `8:0:2`, `3MAT6quo` `1:10312:30:1`, Burst Frog user unit 30 in the reply's box; 150002 ->
  `8:0:4,3:0:1000000,11:0:200000`, karma +200,000, sphere 37600 in the warehouse.
- `p8B2i9rJ` **MissionContinue** (key `G3FwvQfy5hcxHMen`, URL `/actionSymbol/vcn2pb38.php`,
  `gimuserver/gme/MissionContinue.cpp`, **2026-10-31**): the Yes of the "Continue?" dialog after
  the whole squad died (`MissionGameOverScene::initContinue`; "You can use N Gem to revive all
  Units", N = DefineMst ContinuDiaCnt, 0 here). Body: `Kz7qfSs5` {`k9cxD7Ba` serial id,
  `j3g5P4cq` state} and `5PR2VmH1` {`k9cxD7Ba`, `K2gIYm0h` the battle's suspend data,
  `MissionScene::createSuspendData`, not stored}. Reply: the wallet. Unanswered, the client
  popped "Unsupported request: p8B2i9rJ" and stayed on the dialog (found on Dweller in the
  Darkness Lv.4 with the starter squad). After the reply `loopContinue` calls
  `MissionScene::requestContinue` and the squad revives. Verified on BlueStacks: Yes -> request
  logged -> the three units back at full HP, the battle goes on.
- Flake seen 2026-10-31: after declining the "Background DLC Warning Prompt" (it comes back when
  the day changes), the first home entry died in `HomeScene2::initialize` on a null
  `LayoutCacheList::getObject` (a home layout element not found); the next launch was fine.
- ~~Known cosmetic issue: the texts ENERGY_RECOVER_GEM_RECOVER_ENERGY / _MAX_THRESHOLD show as raw
  keys~~ **fixed 2026-10-31**: the client's UI strings are the APK's `assets/text.csv` (780 keys)
  overlaid by the downloaded dictionary `content/sgtext/sgtext_en_<v>.zip` (3,007 keys, the
  2020 table; 2.19.6 added the three `ENERGY_RECOVER_GEM_*` keys after it). The dictionary is
  listed by the resource table (`F_RESOURCE_MST` row `/sgtext/sgtext_en_338.zip`), and
  `TextDictionary_SG::DictionaryFileUpdateCheck` downloads a file it does not have by exact
  name, so a text change = a new suffix + a re-encoded resource table: `data/sgtext_en_patch.csv`
  (`KEY^text`) + `data/overrides.json` (versions) -> `tools/build_overrides.py` ->
  `deploy/overrides/` (see "MST tables: the .dat format"). The prompt now reads "Fully restore
  your Energy? / Energy refills are free here." (`<br>` breaks lines; `<param=gem_use_count>`
  rendered empty with the 0 price, so the texts carry no placeholder).

**Shop tab (2026-10-31).** It no longer kills the client: BlueStacks is signed in to Google now,
so `StoreController.startBillingService()` finds the billing service (the kill was the
"Billing service unavailable" path). What the tab needed, in order:
- **`/_dlcbundle/layout_shop_topV2.csv`**, the 2.19.6 shop top layout, which the 2022 archive
  never had (it holds the older `layout_shop_top.csv`): the tab showed "DLC ERROR ... Retry
  Download?" for good. The APK's `assets/resourceMap` lists, per scene id, the files a scene
  downloads (`700:layout_shop_topV2.csv,shop_top_btn_base.png,...` for `ShopTopScene`); nothing
  bundles the layout itself, so it is ours: **`data/content/_dlcbundle/layout_shop_topV2.csv`**,
  copied by `build_overrides.py` (which now mirrors the whole `data/content/` tree into
  `overrides/content/`). Rows are `x,y,w,h,type,name,` in 640x960 design units, y from the top;
  `ShopTopScene::setLayoutControl` wants `btn_base` (602x124 toolbar), `buy_coin_btn` (110x110),
  `unit_box_ext_btn`, `item_box_ext_btn`, `hel_fight_btn` (friend slots, enabled only when the
  level table's `add_friend_cnt` is > 0), `buy_stamp_btn` (its image switches to the summoner
  item shop under the Ep3 flag), all 117x71, and `initialize` dereferences `scroll_bar`,
  `scroll_area`, `clip_area` and `bundle_list` (the 2020 bundle categories, none here). The
  toolbar sits at y 250, under the sub header (the calibration: this scene draws design y at
  window `248 + 0.6 y` on BlueStacks). **The client caches a downloaded file for good**
  (`CommonUtils::existsLocalFile` wins over a new download, and the scene re-requests the file
  at every open but keeps its copy): a changed layout only shows after `pm clear` /
  `dev.py client --fresh` or a Full Download, which is why the first copy (toolbar overlapping
  the sub header) stuck through three edits and two relaunches. No root on BlueStacks
  (`adb root` restarts adbd as shell), so the cache cannot be edited from outside.
- **Unit / item capacity** (`ShopUse` types 2 / 3) verified from the client: Unit Capacity +5
  -> request `60IsqxDt` 2, `5gXxT7LZ` 5, 0 gems -> `max_unit_count` 100 -> 105 in the DB; Item
  Capacity +5 -> type 3 -> `warehouse_count` 100 -> 105; the gem balance stays. Cosmetic: the
  confirm dialog says "Use 1 Gem(s)" and "Gems needed 1" although the define price is 0 (the
  notice text above it says "0 Gem(s)"); the server never deducts. After a purchase the scene
  goes home (its Back is the result screen's).
- **Buy Gems** (`ShopBuyCoinScene`): opens the store (`StoreController: opening store`, no
  kill with Google signed in) and sends `29slks49` **UserGemShardInfo** (key `930sDd3i`, login
  info + signal key; `gme/UserGemShardInfo.cpp`), a 2017 feature the scene asks for even with
  `buy_coin_free_shard` 0; unanswered it popped "Unsupported request" and the client restarted
  on OK. The reply `s9d3lksS` is a list of {`1ZF3zLrC` id, `J3uDa91b` count}, empty here: the
  scene shows "0/0 Gem Shards", a Claim button and an empty product list (its products are
  `PurchaseMstList`, not served). The purchase flow itself (`kJs6CQq1` BuyCoinStart key
  `J9P7Bmxa`, `9o3q7SjN` BuyCoin key `DCM60QNb`, reply `B6KhoZ1P` BuyCoinInfo {`3nZrT1bC`
  purchase id, `R93Xmtnk` receipt, `swfHMaeb` signature, `Ek5fF7s6` state, `3ub9EdW2` model
  status}) is not implemented: nothing to buy.
- **Stamp**: opens an empty Stamp list, no request. **Friend Capacity +**: greyed at level 50
  (`add_friend_cnt` 0 in the level table), not exercised.
- `dev.py client --fresh` (`pm clear` + the first-run flow) now polls instead of sleeping
  (see "Dev loop"); a fresh install reaches home in ~85 s with no tap by hand.

**Unit tab (2026-10-30):** View Units, Manage Squad, Fusion, Evolve, Sell and the Town tab
all open **without a single server request**: they work from the unit box, decks and master
tables already received at login. Two stub values mattered: `UserTeamInfo.deck_cost` is
the squad cost limit shown as "COST x/limit" (now taken from `user_level.json` for the
player's level, 100 at level 1 in the 2022 table) and `TwqMChon` are the three helper
squad numbers (now 0,1,2). Leaving Manage Squad still shows "The same Unit has been placed
in all Helper Squads": the stub has one unit and it leads all ten squads, so it is true;
OK dismisses it, and it disappears once decks differ (persistence, step 4). The server-side
operations of this tab (DeckEdit `m2Ve9PkJ` is a stub, UnitMix/UnitEvo/UnitSell/
UnitFavorite are not registered) only fire when something is changed, not when browsing.
Town shows Imperial Capital Randall and Karl's greeting from town data alone.

Fixed on the way (both in the fork):
1. `6K7LToCD` (advertising id) in `LoginInfoReq` + `error_on_unknown_keys=false` in the
   Initialize/UserInfo reads (commit 80d33eb).
2. Missing `/content/*.png|jpg` now get a valid 1x1 transparent PNG (200) instead of 404; the
   path is still appended to `deploy/log/dlc_404.log`. The client shows a blocking "Error
   occurred during download" popup on any failed image, and the archive lacks ~850 files
   (`decompfrontier/docs/source/pages/Status/missing-asset.rst`), first hit:
   `/gacha/gacha_Light_Featured_img_2022_s2.png` (commit df95d7a).
3. `GatchaList` handler (`gimuserver/gme/GatchaList.cpp`) + `GatchaListResp` in
   `net/handlers.kdl`: the client maps the top-level key `1IR86sAv` to `GachaInfoResponse`
   (`GameResponseParser::getResponseObject`), i.e. the same `GachaInfoMst` list that UserInfo
   sends, so the handler returns the cached `gacha_info` table. The old stub error killed
   the client one second after the home screen.

Client-side quirks: on first launch the Java side opens a **Google Play Games sign-in**
(`com.google.android.gms/.auth.uiflows.minutemaid.MinuteMaidActivity`); press Back, the game
continues. The title screen waits for a tap ("TOUCH SCREEN"). The server clock is shown as
PST in the client ("Oct 30, 02:39 PST"), harmless for now.

**Driving BlueStacks from the PC** (ADB enabled in BlueStacks settings > Advanced; it listens
on `127.0.0.1:6610` here, the port may differ):
```powershell
$adb = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Google.PlatformTools_Microsoft.Winget.Source_8wekyb3d8bbwe\platform-tools\adb.exe"
& $adb connect 127.0.0.1:6610
& $adb -s 127.0.0.1:6610 install -r D:\.Code\bravefrontier\build\bf-192.168.1.14-9960.apk
& $adb -s 127.0.0.1:6610 shell am force-stop sg.gumi.bravefrontier
& $adb -s 127.0.0.1:6610 shell monkey -p sg.gumi.bravefrontier -c android.intent.category.LAUNCHER 1
& $adb -s 127.0.0.1:6610 shell input tap 960 520          # title screen (BlueStacks is 1920x1080)
& $adb -s 127.0.0.1:6610 shell input keyevent 4           # Back (dismiss the Google sign-in)
& $adb -s 127.0.0.1:6610 shell dumpsys window | Select-String mCurrentFocus
& $adb -s 127.0.0.1:6610 logcat -d -t 500 | Select-String "cocos2d|bravefrontier"
```
Screenshots: `adb shell screencap -p /sdcard/s.png` then `adb pull` (from Git Bash set
`MSYS_NO_PATHCONV=1`; piping `exec-out` through PowerShell corrupts the PNG).

**Server side done:** `deploy/config.json` now has two listeners, `192.168.1.14:9960`
first (what `/dls` advertises) and `127.0.0.1:9960` second. The Ethernet profile is
"Public", so Windows Firewall will drop inbound 9960 until a rule is added (admin):
`netsh advfirewall firewall add rule name="gimuserver 9960" dir=in action=allow protocol=TCP localport=9960`.
The old single-listener config is kept as `deploy/config.json.bak-127only`.

## Friends (2026-11-01)

Friends work end to end: the Social tab (Friend List, Friend Request, Add Friend with the ID search
and "Search From List"), the quest helper picker with friends and strangers, the friend points a
helper earns, and the "send a friend request?" prompts after a quest or an arena battle. Files:
`packet-generator/assets/net/friends.kdl` (every id, key, URL and field), `gimuserver/gme/Friends.hpp`
(the rules and the record builders), `gimuserver/gme/FriendHandlers.cpp` (nine handlers),
`db/Player.hpp` `FriendRow` (table `player_friends`, migration `01112026_Friends`, which also adds
`players.last_login` and `player_runs.helper`).

**How the client works.** It keeps ONE `FriendInfoList`, filled by FriendGet (`2o4axPIC`, sent by
LoginScene, MissionResultScene after every quest, and the random-search scene) and replaced by the
reply of every other friend request (`tojMy68W` -> FriendInfoResponse). Each scene filters that list
by **`96Nxs2WQ` friend_type: 1 friend, 2 a request the player sent ("Pending", Remove), 3 a request
received ("Waiting", Accept / Remove), 4 a stranger** (FriendListUtils::getFriendList keeps 1,
FriendAgreeListScene draws 2 and 3, the ID / random search scenes list 4 and say "already friends"
on 1). The Social top opens the Friend Request list without any request (scene 608,
FriendAgreeListScene; 601 is the connect variant that sends FriendGetAgree `aVqJrR08`), so every
reply carries the whole relation list. A record is the leader unit of one helper squad
(UserUnitInfoBase: unit id, `4A6LzBxr` level, stats, skills...) plus the friend fields: name,
`2Fh3J7ng` level, **`0CAQ6wUe` seconds since the last login ("Online 2 hrs ago"; a unix time
reads as 999 days)**, `98WfKiyA` the 8-digit ID (our account_id, what "Your Player ID" shows and
the ID search sends), `bM7RLu5K` message, `JmFn3g9t` arena rank id, `U4pMNjy0` ABP, `5JbjC3Pp`
favorite, `3InKeya4` the request date **as text** ("Date Requested 2026/11/01 04:29"), `zsiAn9P1`
deck number, `yu18xScw` priority (<= 1 counts). Request bodies: `BPz1e5tU` {`h7eY3sAK` user id} for
FriendApply `WUNi08YL` / FriendAgree `NYKBH4V6` / FriendDelete `SfMN9w4p`, plus `7f3WL6Mx` all and
a comma list for FriendRefuse `N5SQd7oA` (Reject All), {`98WfKiyA` id} for FriendSearch `umNt8M7i`,
{`5JbjC3Pp`} for FriendFavorite `ps6Vgx4R`, `3fTYzqT6` {`3Ug2QynF` 0, `lzpDckWv` 100} for
FriendRandomSearch `mE5gUQOp`; every int of a request body is a string (JsonNode::addParam(int)
quotes). The quest helper picker (ReinforcementSelectScene2) draws **ReinforcementInfoList only**
(`xZH6EIQ7` -> ReinforcementInfoResponse, same unit record + `q9Arz8Jv` friend_type, `edy7fq3L`),
so FriendGet answers the friends first (type 1, the FRIEND ribbon) then other players; its
"Honor +N" is `ReinforcementInfo::getFriendPoint()` = FriendPointInfo (`6e4b7sQt`, one node:
**`4vamo28W` the points for a friend, `KgbM81wd` for anyone else**, each a CSV the client indexes)
chosen by whether FriendInfoList holds the helper with type 1. MissionStart sends the helper's user
id (`h7eY3sAK` of the mission group) and its csv (`1VagK32J`, echoed back): the client builds the
helper unit from the record, the server only remembers the helper in `player_runs.helper`.

**Server rules** (`Friends.hpp`): one row per direction (`state` 1 friends, 2 sent, 3 received);
FriendApply writes both rows (an apply to someone who already asked is an acceptance; refused when
the friend list is full: `UserTeamInfo.max_friend_count` from the level table, 34 at level 50);
FriendAgree needs a received row; Remove on a sent request cancels it; FriendSearch finds the
8-digit ID and answers the one record with the real relation (an unknown ID is an error the client
shows); FriendRandomSearch answers up to 20 random players with no relation (`Db::RandomPlayers`,
level range from the request); the last login is written at UserInfo (`Db::TouchLogin`). **Friend
points**: a helper earns `kFriendPoints` 10 (friend) or `kNormalPoints` 5 at MissionEnd, into
`UserTeamInfo.friend_point` (the honor points that pay Honor Summons); `rein_req_state`
(`0egM9p7I`) is 1 for a stranger, which opens MissionResultFriendRequestScene (the client skips it
when the helper is already in its FriendInfoList, e.g. a request pending). UserInfo also sends
`friend_agree_count` (requests received, the Social tab's badge). The Arena: `friend_req_state` 1
opens ArenaResultFriendRequestScene (`ArenaBattleResultInfo::enableFriendRequest` is `== 1`) for
real players who are not friends, never NPCs; ArenaFriendList (`xkd9H50w`) answers the friends with
an arena squad as FriendUserArenaInfo (`BYaF62TE`, keys in `arena.kdl`) and ArenaBattleStart takes a
named player as the opponent when it is not the suggestion (a friend picked on ArenaFriendListScene).

**Verified on BlueStacks** with three accounts (the dev loop: a second account is one guest login
with a new `vid` plus an Initialize by `gme.py --gumi`): Friend List shows "Tester Two, Lv.1, Online
2 hrs ago, Rookie, 1/34", Friend Info shows the ID, the leader unit and its stats, Add / Remove
Favorite (Remove Friend is greyed for a favorite); Add Friend shows "Your Player ID 73482464",
Search From List lists a stranger, Friend Request -> "Friend Request has been sent."; the Friend
Request list shows the request received (Waiting, Accept) and the one sent (Pending), Accept ->
"Friend Request accepted."; the helper picker lists "FRIEND Tester Two Honor +10" and "Tester Three
Honor +5", a quest with the stranger pays 5 friend points and ends on the "Friend Request" prompt
(-> "Friend Request has been sent."), a quest with a friend pays 10 and shows no prompt (a helper
with a request pending shows none either). Not exercised on the client: Reject All, the ID search
typing, the arena friend fights (ArenaFriendList and a friend ArenaBattleStart by gme.py only).
Beware: `dev.py build` stops the detached server (the link step), which drops a client mid-quest
("Connection error": OK resends MissionEnd once the server is back).

**Tools**: `tools/client/` (README there): `keymap.py` follows a `*Response::readParam` strcmp chain
to the setter each key feeds, `getters.py` lists a class's accessors with their object offsets,
`vt.py` reads vtable slots, `dis.py` / `syms.py` disassemble and list: the way the friend record was
mapped (`FriendInfo` derives from `UserUnitInfoBase`).

## Arena (2026-11-01)

The classic PvP Arena works end to end against NPC summoners (and against other registered
players' arena squads once there are some). Files: `packet-generator/assets/net/arena.kdl` (every
request, response and field, with the client's ids, keys and URLs), `gimuserver/gme/Arena.hpp`
(the rules), `gimuserver/gme/ArenaHandlers.cpp` (the nine handlers), `db/Player.hpp` `ArenaState`
(table `player_arena`, migration `01112026_PlayerArena`).

**How the client plays it.** The home carousel's Arena page (tap (790,690) from home) opens
ArenaTopScene, which sends ArenaStart (`zu6ifpy0`) and draws the record: rank name
(`arena_rank.json`, 101 ranks by Arena Battle Points: Rookie 0-499, Novice 500-1199, Amateur
1200-1999, Warrior 2000-2999, Contender 3000-4999...), wins / losses, the win streak
("Consecutive Victories" comes from a *separate* group, the UserTeamArenaArchive `PQ56vbkI`, not
from UserArenaInfo), points and the bar to the next rank. The first visit plays the tutorial
(scripted Kevin vs Seria battle from `arena_tutorial_npc_info`). Enter sends ArenaMatching
(`82ztj6Iy`) and shows one opponent (ArenaSuggestEnemy: name, level, rank, leader unit, record);
Refresh asks again. Battle!! -> "Start Battle? 1 Arena Orb will be used" -> ArenaBattleStart
(`7f1rg92L`): the server spends an orb and returns **both squads as NPC JSON strings** (the
picojson format of `NpcTeamParse` / `NpcPartyParseList` / `NpcUnitParseList`: every value a
string; team `{P_USER_ID, P_LV, P_FRIEND_MESSAGE, P_GUILD_ID}`, party `[{P_USER_UNIT_ID,
P_MEMBER_TYPE (0 = leader), P_DISPORDER}]`, units `[{P_USER_UNIT_ID, P_UNIT_ID, P_UNIT_TYPE_ID,
P_LV, P_HP, P_ATK, P_DEF, P_HEL, P_SKILL_ID, P_SKILL_LV, ..., P_EQP_ITEM_ID, P_IMG_TYPE, P_FE_INFO,
P_OMNI_LV}]`): the enemy in `9NX83YVe` (ArenaBattleEnemyInfo) and **the player's own arena squad in
`5K5wsBoL`** (the client builds its party from that, not from the box). The battle is automatic
on the client (about a minute). ArenaBattleEnd (`oQNtKU51`) sends the log (group `yTEvm85f`:
`3pAQY5KW` result **1 = win, 0 = loss** (verified both ways), BC / HC collected, units knocked
out and their ids, damage, healing, overkills, perfect, time-out...) and gets the scoring back;
the result screens, then the "send a friend request?" prompt (ArenaResultFriendRequestScene),
then the top again. The Record button sends GetPlayerInfo (`vUQrAV65`, login info only) and
ArenaArchiveScene draws the statistics from the archive group. With 0 orbs the top offers
"Insufficient Arena Orbs: use 0 Gem to fully restore" -> ShopUse type 4 (free here).

**Server rules** (`Arena.hpp`, all ours; the original's exact numbers are unknown):
- Orbs: `UserTeamInfo.fight_point` / `max_fight_point` (3 from the level table), one per battle,
  one back every DefineMst `recover_time_fight` seconds (3600 in the served defines), the clock
  in `player_arena.fight_at`; the client shows the countdown next to the orbs. Regenerated at
  login (UserInfo) and on every arena request. ShopUse type 4 refills (0 gems).
- Points: a win is +20 plus (enemy points - mine) / 50 clamped to [-10, +20]; a loss or a retreat
  (ArenaRetire `eyg8sA32`) is -10, never below 0. Karma 300 x the rank id for a win, 100 for a
  loss. Rank-up rewards from `arena_rank.json` (`reward_type` "1" = gems, "2" = an item
  "id:count"), paid once per rank in `ArenaBattleEndResp.reward` (`okd0y3Ir`) with
  `first_rank_up` 1; `max_rank` remembers what was paid.
- Opponents (`Arena::Pick`): the other players' arena squads (`Db::ListOtherPlayers`, deck type 1
  number `arena_deck_num`, the active squad when it is empty) and, while there are fewer than
  three, NPC summoners: five Rare Summon pool units at about the player's level, user unit ids
  9000000+, points near the player's, a fixed name list (Gladiator Kevin, Knight Lorand, ...),
  deterministic per day and index. The one just shown is not shown twice in a row. The opponent
  and the battle id live in `player_arena` (`enemy`, `battle_id`) until End / Retire, so
  ArenaBattleRestart (`NPyXDgV9`, resuming) can hand the same squads back.
- Statistics: `UserTeamArenaArchive` (group `PQ56vbkI`, 33 fields, see `user.kdl`) is stored as
  JSON in `player_arena.archive` and accumulated from the battle log (knock-outs by element and
  rarity from the unit table, BC / HC, damage, healing, overkills, perfect and time-out wins,
  points won); the streak and best streak come from `series_win` / `max_series`. Sent in UserInfo
  and every arena response.
- ArenaFriendList (`xkd9H50w`) answers the friends with an arena squad (see "Friends"); ArenaInfo
  (`oim9TU1D`, the arena place select) = ArenaStart.

**Known gaps.** Defence battles (being attacked), bonus points / random rules and rankings are
not simulated; the "accept" counts are the defender's record when a real player is attacked
(ArenaBattleEnd adds a defence, won when the attacker lost). `current_position` is always 1. The
client has no arena ranking request at all (ArenaStart / Info / Matching / BattleStart / BattleEnd /
Retire / BattleRestart / FriendList are its only arena requests), so there is nothing to rank. (The
friend request prompt is right now: `friend_req_state` 1 opens it, so it is 1 only for real
non-friends, see "Friends".)

**Packet notes.** `[X]::size(1)` request groups arrive as one-element arrays (`"fq2C03jh":[{...}]`);
send them that way with `gme.py`. The arena import in `handlers.kdl` must come after `user.kdl`
(ArenaStartReq embeds LoginInfoReq). A migration that already ran locally is reset with
`sqlite3 deploy/gme.sqlite "DROP TABLE player_arena; DELETE FROM migration_status WHERE hash='01112026_PlayerArena'"`
(the platform-tools folder has sqlite3).

## MST tables: the .dat format (2026-10-31)

The `/mst/Ver<version>_<file key>[_<part>].dat` files the client downloads are **base64 of
AES-128-ECB / PKCS#7 over a plain JSON array of rows**, the AES key being the table's 8-character
*encode key* right-padded with zero bytes to 16 (the GME body scheme; the decrypt lives in Java,
`BraveFrontierJNI.decodeCStringForBase64(data, key)`). The two keys per table come from a string
table in `libgame.so` (`DataMstManager::getMstFileNameFromKey`); `tools/mst_keys.json` holds the
117 recovered pairs (`tools/mst.py keys --lib libgame.so` tries every 8-character string of the
library on the served files and keeps the one that decodes to the row count of
`version_info.json`). `version_info.json` rows: `moWQ30GH` name, `d2RFtP8T` version (bump = the
client re-downloads), `H6k1LIxC` parts, `5kbnkTp0` rows (`checkCorruptionsOfMsts` compares). The
original files are PHP `json_encode` output (compact, escaped slashes, `\uXXXX` for non-ASCII);
`mst.py encode` reproduces them byte-for-byte in size. `python tools/mst.py decode F_RESOURCE_MST
-o rows.json`, `python tools/mst.py encode F_RESOURCE_MST rows.json --version N --out
server/deploy/overrides/mst`. `BfWebController` serves `deploy/overrides/<content|mst>/...` before
`game_content` (a read-only symlink into the decompfrontier clone). First use:
`tools/build_overrides.py` rebuilds the UI text dictionary (`sgtext_en_340`, the archive's 338 +
`data/sgtext_en_patch.csv`) and the resource table (`F_RESOURCE_MST` 3277, one path changed;
`data/overrides.json` holds both versions and the tool writes the new version into
`version_info.json`). Run it after a clone (`deploy/overrides` is not tracked), then `dev.py
reload`. Verified: the client fetched `Ver3277_hdt2N6mr_{1,2}.dat` and `sgtext_en_340.zip` at the
next login and the energy prompt shows the new text.

**Phase filter (2026-10-31, `tools/build_phase.py`, run by `build_overrides.py` from
`data/overrides.json` `"phase": {"cutoff": "2014-10", "bump": N}`).** A phase is a cutoff month
on `data/*_timeline.json` (first month each unit / mission was seen in Global; keyed by id where
the datamine snapshot was, by name for the Aug-2014 snapshots, a named entry being matched to the
id-keyed entry of the same name, plus land / area for missions). Kept: units first seen <= cutoff
(724 for 2014-10; one collab name, "Deemo and the Girl (5*)", has no id), the evolutions whose
source *and* target are kept (431 of 1,342: a later 6*/7* form appears with its phase; the unit
materials of kept evolutions would be pulled in, none needed for 2014-10), unit comments by unit,
missions first seen <= cutoff (696 of 3,433: lands 1-6 and 235 Vortex missions, no kept mission
requires a dropped one), and the dungeons / areas / lands that still hold a mission (173 / 78 / 7
with Vortex), mission scripts and EP3 rows by mission (EP3 ends up empty, a 0-row table is fine).
Skills, items, resources and the rest are served unchanged: the client only reaches them through
units and missions. Every filtered table is re-encoded into `deploy/overrides/mst/` at the
archive's version + `bump` (`mst.archive_version` reads the base from the file names, so bumps
are repeatable), `version_info.json` gets the versions and the new row counts
(`checkCorruptionsOfMsts` compares them), and the kept id sets go to `deploy/phase.json` for
server-side enforcement (not done yet). Raise `bump` whenever the output changes: the client
keeps a table until its version moves. Verified: after a bump the client fetched all 11 files
(`Ver1086_2r9cNSdt_{1,2}`, `Ver1120_4gA3WCQX_{1,2}`, `Ver1004_B57MJYoF`, `Ver671_vj6fEsg7`,
`Ver149_1M65tIPz`, `Ver599_Ef6oG0mz`, `Ver707_2npmC9Nw`, `Ver290_F2Dz3QHU`, `Ver59_W3sQD9Z5`) at
the next login and reached home, the Mistral map and the Vortex screen; the quest map's "Grand
Quest" button is gone (its missions are out of phase). Not visible yet: the land select (the
Quest button opens the last land's area map directly), a unit catalogue (none in this client's
Unit tab), and the Vortex list is empty (it needs a server-side event schedule, same as before).
Lesson from the verification: the client's MST download burst at login (11 files in one second)
used to be invisible in `deploy/log/`: `tryOpenHttpDumpLog` named dumps by the second, so the
burst shared one garbled file. The names now carry a per-process counter (`_NNNN`) and
`dev.py logs` understands both forms.

**Summon rates display (2026-10-31).** The 2.19.6 client has no rates dialog (Revive's "Rates"
button is their client patch; the summon "Details" screen, `SummonsDetailScene`, is a native
layout of `detail_msg` and tickets). Two channels, both server-side:
- **The summon texts**: `ServerCache::Setup` appends the pool's odds to each pooled banner's
  `detail_msg` (the box under the banner) and `caption_msg` (the confirm dialog) after loading
  `gacha_info.json` and the pools: "Rates: 3* 70% (41 units), 4* 30% (62 units)" with the
  client's `<br>` / `<color=r:g:b>` tags. Verified in the Honor Summon dialog.
- **A web page with owned marks**: `/bf/web/rates/index.php?friend_id=<id>` (`controller/
  WebPages.cpp`, dispatched by `BfWebController::HandleWebPage` after collapsing the leading
  `//`: the client builds URLs as `http://<host>/` + path, so they arrive as `//bf/web/...` with
  `?os=2`, hence the second regex route in `BfWebController.hpp`). Per summon: each rarity with
  its rate and unit count, then every unit with its own odds (rate / units of the tier), names
  from `gacha_pools.json` `names` (build_pools.py reads `sgtext_units`), and a check mark when
  the player's box holds the unit or one of its evolutions (same `group` in units.json). The
  player: the Links page (MENU > Links, `BannerLinkTopScene`, `banner_info.json`) appends
  `?friend_id=<UserInfo friend_id>` when a banner's `param` is `?friend_id=`; the server now
  sends `friend_id` = `account_id` and finds the player with `Db::LoadPlayerByAccountId`.
  `phases/01/banner_info.json` adds banner 8001 "Summon rates" (url `http://{host}/bf/web/
  rates/index.php`, `{host}` replaced at load with the advertised host; image
  `banner_summon_rates.png`, 600x120, tracked in `data/` and copied to `overrides/content/
  banner/` by build_overrides.py; the client caches a banner image by name for good, so a
  changed image needs a new name). **A Links banner opens the system browser**, not an in-app
  web view (Chrome on BlueStacks, with its first-run page the first time), and the tap also
  sends `a5k36D28` **BannerClick** (key `a63Ghbi2`, group `Pk5F1vhx` {`oL71Fz3a` banner id}, a
  click counter), answered `{}` by `gme/BannerClick.cpp`; unanswered it popped "Unsupported
  request" when the game came back. Verified: the page renders (dark theme, no external
  assets) with 18 owned marks for the test account. The notice page (url.json `notice_url`
  `/bf/web/notice/indexCtrl.php`, MENU > **News**, not Info = Tips & Information) is served as a
  small phase summary with a link to the rates page, but **the client cannot show it yet**: News
  opens an in-app WebView (`BFWebView: Showing webview: http://<host>:9960//bf/web/notice/
  indexCtrl.php?friendId=<id>&os=2`, note `friendId`) and Android's cleartext policy blocks the
  plain-http load before it reaches the server (blank page with a broken-image icon; the
  manifest has no `usesCleartextTraffic`, see "Android client"). Fix = patch the manifest in
  `tools/patch_apk.py`; the Links banner works because it goes through the system browser.

**Parade keys (2026-10-31).** The 2014 Metal / Jewel Parades opened for an hour per key, keys
being daily-login presents on Mon/Tue/Thu/Fri (Metal) and Wednesday (Jewel). The 2.19.6 client
carries the 2015 key system, so no item is involved: **`DungeonKeyMst`** (Initialize `4NG79sX1`,
`deploy/system/dungeon_keys.json`, upstream's: `16KMNJLb` id, `BM29ZgnK` name, `MHx05sXt` the
dungeon it opens, `i9sBW8uD` window seconds, `N7I9vYZb` possession limit, `EK5I6MQ9` distribute
flag, **`fK15a73g` the weekdays it is handed out, "1,2,4,5" / "3", exactly the 2014 days**,
`CR6aKWg8` the tiers "keys:event:name:missions:x/..." = 1 key Metal Parade! 100600, 3 keys Super
100601, 5 keys Mega 100614; `phases/01/dungeon_keys.json` keeps Metal + Jewel, an hour, no Mega
tier), **`UserDungeonKeyInfo`** in UserInfo (`eFU7Qtb0`, replaced whole: `wgV86x1q` possession
is what the Vortex shows as "x2", `KI42FwRt` last receipt day, `BY8fZ7M1` active type,
`g85qMNxf` receipt possible, `b6QR1CH5` next receipt date, `H6k1LIxC` cnt; `net/user.kdl`),
and `features.json` **`dungeon_key` 1** (phase 1 now; with 0 the key dungeons are plain
schedule entries). How it plays: `AreaSPSelectScene` lists a key dungeon (one that
`DungeonKeyMstList::getObjectAtDungeonMst` knows) from its permit like any other, draws it
chained with the key's `9unNZ6b0` seal banner while `C1vG0iKh` enterable is 0, shows the key
counts under the list; a tap opens `DungeonKeyConfirmScene` (the tiers; the "5 keys / Mega"
button shows even without the tier in the table, the server refuses it), the tier's Yes sends
`aGT5S6qZ` **DungeonKeyUse** (key `3rPx6tTw`, URL `/actionSymbol/RmA2a5JM.php`, group
`xki65BPq` {`phe9Ta54` keys to spend, `16KMNJLb` key id}; `gme/DungeonKeyUse.cpp`), and the
reply (`eFU7Qtb0` keys left + `yXNM8kL3` the whole permit list) goes straight into the
dungeon's mission list. Server side: `player_keys` (count, day of the last grant) and
`player_parade` (per dungeon: until, the tier's missions), migration `31102026_PlayerKeys`;
`gme/Keys.hpp` `Infos()` builds the key records and **grants one key per key-table row whose
weekdays hold the game-clock weekday, once a day, at UserInfo** (the Randall key building,
`RandallKeyScene` / `GetDistributeDungeonKeyInfo` / `DungeonKeyReceipt`, stays closed with
Randall); `Vortex.hpp` entries with **`key`** are permitted at all times (area + dungeon,
`enterable_flg` and `time_limit` from the player's window, the window's missions only), the
`Permits` / `IsOpen` of UserInfo, UpdatePermitPlaceInfo and MissionStart take the player's
`ParadeMap`. Dev: `/bfanew/give?user=<user id>&key=<id>&count=N`. Texts: the tier-select dialog
rendered its `<param=...>` blanks ("You can use (s) to enter ."), so `USE_KEY_DESCRIPTION` and
`USE_KEY_HAVEKEY_NUM` are placeholder-free in `data/sgtext_en_patch.csv` and
`USE_KEY_CONFIRMATION2` says "1 hour" (`sgtext_en_341`, F_RESOURCE_MST 3278); the Yes/No
dialog fills its params fine. Verified: `gme.py` login -> both keys at 0, Parade Garden permits
not enterable; a key -> Metal Parade! enterable for 3600 s with mission 100600 only, Super
refused, 5 keys refused; the client's Vortex lists "Parade Garden" -> "METAL PARADE! Remaining:
58min" and "Jewel Parade!!" chained with x2 / x1 keys, tap -> tiers -> "Use 1 Jewel Key(s)...
Jewel Key(s) on hand: x 1" -> Yes -> `DungeonKeyUse` key 2 -> the Jewel Parade! mission list
(0 energy, 5 battles); the daily grant: Saturday added to the Metal Key's days for the test ->
2 -> 3 at the first login, unchanged at the second, `last_day` 20757.

**Feature gating (2026-10-31).** Three client mechanisms, all fed by the server:
- `features.json` (`/bf/gme/featureCheck.php`, parsed by `FeatureManager::onFeatureCheckResS`;
  only the keys of `net/feature_check.kdl` are served, and the client spells three of them
  `video_ads_slots`, `old_video_ads_slots`, `full_unit_ills_exclude` and reads
  `buy_coin_free_shard`, now in the kdl; a `_doc` key in the file makes the load fail). Phase 1
  (`deploy/system/phases/01/features.json`) turns off randall + randall_facility +
  randall_library_memories, raid + raid_battle_restart, guild + guild_visible, colosseum_enable,
  frontierhunter, frontiergate_plus, challenge_arena, sandbag_enable, fish, bf_campaign_grand_quest,
  mystery_chest, video_ads + video_ads_slots + slot, sg_resummon_gacha_enable, trial, new_164_trial,
  bf_achievement, unit_type_bonus_skill, va_sp_skill, feature_gate (the 2016 level locks), dungeon_key
  + dungeon_key_cnt, multisummon, social_special, sg_target_bundle_flag, dailylogin_gem. Verified:
  the Social tab loses Guild Inbox; the home EXCHANGE popup greys Guild Shop and Merit Shop (Bazaar
  stays); the home orb popup greys Grand Quest, Frontier and Trial (`toggleOverlayMenuShortcut`
  also needs a Grand permit, a finished "randall" scenario and a login campaign for those).
- **Function release** (`Dp0MjKAf` UserReleaseInfo in UserInfo, `UserReleaseInfoList::isRelease`:
  true unless an entry with that function id says 0): `phases/01/release_info.json` closes
  function 1, the village's Randall icon (`MyTownTopScene::initialize` -> `imperialIconSam`);
  the `randall` flag alone does not hide it. Verified: the icon is gone. F_FUNCTION_RELEASE_MST
  lists functions 8-19 with a `2:<mission>` condition, not touched.
- **Feature gate** (`FeatureGatingHandler`, level locks with unlock rewards): off in phase 1
  (`feature_gate` 0) so nothing is locked by level, like 2014.
Leftover: the "Daily Spin" shortcut (DailyLoginScene, 2016) has no flag; `HomeScene2::
initOverlayMenus` always builds it and a daily-login campaign id of 0 in Initialize crashes the
home scene (null reward lookup), so it stays, opening with "Spins: 0/0". The village facility
list is its own gate (`TownFacilityMst.need_mission_id`, see "Town facilities").

**Summon pools (2026-10-31).** `tools/build_pools.py` (run by `build_overrides.py`) writes
`deploy/system/phases/<NN>/gacha_pools.json`: per summon (gacha id) the draw weight of each
rarity and the unit ids of that rarity, from `deploy/phase.json` and the served `F_UNIT_MST`.
`unitGettingType` (`6yHMXYv1`) tells the families apart: **2 = Rare Summon families** (Firedrake,
Knight Agni... and their evolutions), **3 = common / quest units** (the starters' first forms,
Burny, Witch Liza; the Honor Summon), 1 = evolution-only forms (Burning Vargas and up). A pool
takes the **base forms** only (units no evolution of the phase produces) and plain units
(`unitKind` `xP9oi5Z2` 0: no frogs, crystals, nymphs, jewel ghosts). `data/overrides.json`
`pools` holds the odds: `per_unit` true + `rarities` (every unit of the pool has the same odds, a
rarity's weight = its unit count: Rare Summon 3* 41 units / 4* 62 = 40% / 60%, 0.97% per unit;
Honor Summon 1* 18 / 2* 52 = 26% / 74%, 1.43% per unit), or explicit `rates` {rarity: weight}.
Phase 1's one 5* base, "Xenon & Estia", is a 2015 collaboration the timeline matched by name and
the three 4* honor units are later collaborations, both left out by not listing the rarity.
**Why flat per unit (2026-10-31):** gumi never published the 2014 odds (searched: the Global and
Europe wikis, their 2014 Wayback copies, GameFAQs, the Japanese wiki; Reddit is closed to the
tools); when it did publish rates, from 2018 (Apple's loot-box rule), its legacy pools were flat
per unit ("Legacy Summon: evenly distributed rates", "Brave Summon: each unit 2%", Super Rare
Summon Plus by batch group), and the 2015 player reports (mostly 4* pulls, a few 3*) match a
per-unit draw over a pool with more 4* than 3* bases. The 2014 wiki copy (Wayback Oct 23, 2014)
confirms the pool shape: Rare Summon "3* rarity and better" for 5 gems, Honor Summon 1* and 2*,
rate-ups giving the featured units "a minimum 4-star". Verified: GatchaList texts "Rates: 3* 40%
(41 units), 4* 60% (62 units)" / "1* 26% (18 units), 2* 74% (52 units)", the rates page shows
0.97% on every Rare Summon unit and 1.43% on every Honor one, 12 Rare Summons drawn by `gme.py`. `GachaAction` draws the rarity by weight, then a unit uniformly, builds the
unit from `deploy/units.json` (level-1 stats, element, leader skill, brave burst) and keeps the
six-starter table only as the fallback when no pool file is served. Verified: 8 Rare Summons by
`gme.py` -> 4 three-star and 4 four-star units of 7 different families, 1 Gem each; the client's
Rare Summon shows the summoned unit.

**Phase enforcement (2026-10-31).** `gimuserver/db/Phase.hpp`: `ServerCache::Setup` loads
`deploy/phase.json` (optional: without it nothing is enforced and the startup log says so) into
id sets; `phase().hasMission(id)` / `hasUnit(id)`. MissionStart refuses a mission outside the
phase ("Not available yet"), UnitEvo an evolution whose target is outside it ("This unit cannot
evolve yet"), GachaAction never hands out a unit outside it (the pool is re-rolled, an empty pool
is an error). The client never asks for those with the filtered tables; the checks are for
modified clients. `reload` re-reads the file. Tested with **`tools/gme.py`** (sends a GME request
as a player: the AES-ECB envelope, the handler keys read from `GmeControllerHandlers.cpp`, the
reply decrypted): `MissionStart` 1577 (Celgrad) -> error `Not available yet`, mission 12 ->
a normal start. `python tools/gme.py MissionStart '{"9Q1Lq5FS":[{...}]}'` (see its docstring).

**Vortex schedule (2026-10-31).** The Vortex (land 99, the quest map's Vortex button,
`AreaSPSelectScene`) is driven by **permits**: `AreaMstList::getActiveList` lists the land-99
areas that have a `PermitPlace` record (`yXNM8kL3`), `isLockDungeon` needs the dungeon's record
(`enterable_flg`), `strPeriod` shows `period_text` after `OPEN_PERIOD`, the banner's
"Remaining: 1d" comes from `time_limit` (seconds), and the mission list inside wants mission
permits. Areas with `sHMai2ZG` (F_AREA_MST "parent area") are listed under a **folder area**
(100002 "Travellers' Saga", 100004 "Seasons Past"; 100001 "Enhancement" is a plain area holding
the weekday dungeons), which must be permitted too and must exist in the filtered F_AREA_MST
(`build_phase.py` keeps a folder whenever a child is kept). **`deploy/system/vortex_schedule.json`**
(force-added, re-read by `reload`): `utc_offset_hours` (-8, the PST clock the client shows as
"Server Time") and entries `{area, dungeons, weekdays (0 = Sunday), hours [[start, end)...],
period_text}`. `gimuserver/gme/Vortex.hpp` turns the entries open now into permits (gate 99,
land 99, folder + area, dungeon with the seconds left, the dungeon's missions from
`deploy/phase.json` `dungeon_missions`) appended to UserInfo's `permit_place`; `1MJT6L3W`
**UpdatePermitPlaceInfo** (key `3zip5Htw`, URL `/actionSymbol/AsTF24Gn.php`, login info only)
answers the whole list again when a timed permit ran out; MissionStart refuses a Vortex mission
whose dungeon is closed ("This dungeon is closed right now"). Verified (Saturday): `gme.py`
UpdatePermitPlaceInfo -> 46 permits (5 dungeons open), Monday-only 100050 refused, 100500
started; the client's Vortex lists Enhancement, Travellers' Saga and Deemo with "Remaining: 1d",
Enhancement -> Golden Slumberland -> Gold Valley Lv.1-3 unlocked. Not verified: a permit expiring
while the client is open (the UpdatePermitPlaceInfo round trip), hour windows (Parades).

**The 2014 Vortex calendar (2026-10-31)** replaced the guessed rotation. Evidence: the capture
file names of `cheahjs/bravefrontier_data` `missions_parsed/` carry the capture time
(`<name>_<YYYY-DD-MM-HH-MM>.json`, 191 Vortex captures Oct 1-23, 2014) in **UTC+8** (the only
offset under which Phantom Pots lands on Tuesday PST, Box of Desires on Wednesday and Ruins of
Thunder on Thursday, the weekdays the 2014 gamelytic farming guide names), the Europe wiki's
2014 daily pages (Monday karma = "Sacred Land of the Souls" 13/20/25 energy, i.e. dungeon
**100000** "Gathering of Souls"; 100050 "Soul Sanctuary" 8/10/13 is the 2017 Super Karma
Dungeon, the schedule had the wrong one) and the Global wiki's event archive (Metal keys
Mon/Tue/Thu/Fri, Jewel keys Wednesday; the Sibyl Sisters re-run of Jan 2016 ran each dungeon two
days with a one-day stagger, exactly the pattern of the Oct 6-12, 2014 captures). What the
captures date: Deemo Oct 1-6, Keymaster's Eclipse Oct 4-5, Sibyl Sisters Oct 6-12, Miku Oct
9-10, Grand Gaia Chronicles and Trial of the Gods from Oct 17-18, Unholy Tower from Oct 22, XE
Prologue Oct 23, Super Metal Parade on weekends, elemental Metal Parades on any day, Vortex
Trials 001/002 (Oct 4). The schedule format grew three fields (`gimuserver/db/Phase.hpp`,
`gme/Vortex.hpp`): **`cycle_weeks` + `weeks`** (an event rotation; weeks start on Monday and are
numbered from 1970-01-05, so week N is the same calendar week for every entry and a Sunday
belongs to the week of the Monday before it), **`dates`** (`[from, to]` YYYY-MM-DD in the game
clock, one-offs) and **`missions`** (a subset of the dungeons' phase missions: Super Metal Parade
on weekends without the plain one, Wandering Spirit Lv.1-4 without the 2018 Lv.5, Trials 001-002
without 003). **`GET /bfanew/vortex[?at=YYYY-MM-DDTHH:MM]`** (`DevController::HandleVortex`,
`Vortex::Describe`) prints what is open at any game-clock time, so the whole cycle is checkable
without waiting for the days. Phase 1 (`phases/01/vortex_schedule.json`, 44 entries): Monday
Gathering of Souls + Ruins of Light and Dark (one capture, Oct 6 00:09 PST), Tuesday Faerie
Paradise, Wednesday Cave of Greed + Jewel Parade, Thursday Garden of God, Friday Remains of
Eternity, weekends Golden Slumberland, the Parades by key (see "Parade keys"; the `key` field
of an entry), Grand Gaia Chronicles / Vortex Trials / Trial of the Gods always, and a **six-week event
rotation** of the Oct 2014 events (week 0 Sibyl Sisters staggered Mon-Sun, 1 Deemo + Keymaster's
Eclipse, 2 Miku + Battle Maidens staggered, 3 Unholy Tower + Dark Side + XE Prologue, 4 Leaders
of El Gaia staggered, 5 Thunder Turnout) with one Travellers' Saga dungeon (100700-101200) per
week; the order of the rotation is ours. Left closed: the 2015+ content the Oct 2014 table
already carried (Year-End / Paradise dungeons, Heroes' Challenge 801006..., Moonlight Parade,
Hellborn Nightmare, Frontier Hunter, Burny & Friends). Verified: `/bfanew/vortex` for a Monday
noon (karma, Ruins of Light and Dark, Metal Parade ending in 30 min, the week-2 events), a
Wednesday 20:30 (mimics, Jewel Parade, Serin ending in 3h30 and Bayley in 1d 3h30), a Sunday
23:59 (everything ending in 1 min); `gme.py` MissionStart of Trial 003 and of a closed Super
Metal Parade -> "This dungeon is closed right now"; UpdatePermitPlaceInfo -> 9 areas, 10
dungeons, 34 missions, "Weekends" / "This week" period texts, time limit 131874 s; the client
(Saturday) lists Enhancement, Travellers' Saga, The Keymaster's Eclipse and Deemo with
"Remaining: 1d", Enhancement -> The Golden Vault, and a folder entry whose dungeon is closed
(the Travellers' Saga folder lists every child area of F_AREA_MST) bounces back to the Vortex
top, as a non-permitted area does. Cosmetic: the 2022 banner art carries later weekday labels
("Thursday Dungeon" on The Golden Vault).

## Next steps (in order)

1. Android patch: **done** (script, APK; home, Summon with a working Honor/Rare summon, Unit and Town browse without requests). Quests play end to end with the 2014 battle data (see "Quests play end to end"); next on the client side: the write operations (DeckEdit, fusion, sell) and item pickups once persistence exists, enemy skills in `build_battles.py`.
2. ~~Fork decompfrontier/server into bfanew~~ **done 2026-10-30** (`bfanew/server`, subtree with history; first fix: Android `6K7LToCD` key + tolerant JSON reads). ~~Still to do: the `phase` config + `phases/NN/` folders for login-time tables~~ **done 2026-10-31** (see "Phasing design" 1).
3. MST tooling: ~~decode/encode `.dat`~~ **done 2026-10-31** (`tools/mst.py`); ~~the phase filter driven by `data/*_timeline.json`~~ **done 2026-10-31** (`tools/build_phase.py`, see "MST tables: the .dat format"). ~~Server-side enforcement from `deploy/phase.json`~~ **done 2026-10-31** (MissionStart, UnitEvo, GachaAction). ~~A Vortex schedule for phase 1~~ **done 2026-10-31** (see "Vortex schedule"). ~~Phase folders for the login-time tables~~ **done 2026-10-31** (`deploy/system/phases/01/`). ~~The real summon pools~~ **done 2026-10-31** (see "Summon pools"). ~~A real 2014 Vortex calendar~~ **done 2026-10-31** (see "The 2014 Vortex calendar"); ~~Parade keys~~ **done 2026-10-31** (see "Parade keys"). ~~Real summon odds~~ **done 2026-10-31** (flat per unit, the only documented rule; see "Summon pools"). Still to do: moving the other phase-1 edits of `deploy/system` (banners, campaigns) into the folder as they are made.
4. Core loop: ~~battle data for every phase-1 mission~~ **done 2026-11-01** (synthesized where no capture exists, see "Battle data synthesis"); ~~accounts and persistence → unit box → quests and rewards → summon~~ **done 2026-10-30** (see "Persistence"); ~~the warehouse / items~~ **done 2026-10-30** (see "Items / warehouse"; items used in battle still unverified on the client); ~~the village crash~~ **solved 2026-10-30** (content archive gap, see "Village crash"); ItemSell and ItemMix verified in the village; ~~energy purchase~~ **free economy done 2026-10-30** (see "Free economy"); ~~town facility upgrades~~ **done 2026-10-31** (TownFacilityUpdate); ~~item collection taps~~ **done 2026-10-31** (TownUpdate); ~~the energy-recovery texts~~ **done 2026-10-31** (patched dictionary via overrides); ~~summon odds~~ **done 2026-10-31** (see "Summon rates display"); ~~unit / item first-clear rewards~~ **done 2026-10-31** (see "First-clear rewards"); ~~the Shop tab~~ **done 2026-10-31** (see "Shop tab"; buying gems stays unimplemented on purpose).
5. ~~Test feature flags system by system~~ **done 2026-10-31** for the menus reachable today (see "Feature gating"); ~~the Shop tab~~ checked 2026-10-31 (see "Shop tab"); ~~the Arena~~ **built 2026-11-01** (see "Arena"; defence counts 2026-11-01, no ranking request exists in this client); ~~friends~~ **built 2026-11-01** (see "Friends").

## Conventions

- Don't modify the `decompfrontier/*` clones at all any more; all server work happens in `bfanew/server`. To pull upstream later: `git subtree pull --prefix=server ../decompfrontier/server main` (and the same for `server/packet-generator`).
- Published at https://github.com/hyrasch/bfanew (public, AGPL-3.0). Push `main` after each milestone (`git push`). Never commit the APK, assets, keystore or Revive's build.
- Git identity: `Sylvain Franco <sylvain.franco@outlook.fr>`. End commit messages with:
  ```
  Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
  ```
- Non-commercial fan project. Server code stays AGPL-3.0, so source must be offered to anyone using a public server. The APK, art and data belong to Alim/gumi.
