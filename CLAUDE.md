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
UnitSell, ItemEdit (equipped items), ItemSell. Stubs: DungeonEventUpdate (story events watched,
not stored), UpdateEventInfo (home screen 5-minute poll). Not implemented: item synthesis (ItemMix), town facilities, arena, gifts, friends,
shop. The village (Town tab) works again: its crash was a content-archive gap, not a packet (see
"Village crash" under "Items / warehouse"). Handler registry:
`gimuserver/gme/GmeControllerHandlers.cpp` (`REGISTER(id, func, aesKey)`). Login-time tables are
loaded in `gimuserver/drogon/ServerCache.cpp` from `deploy/system/*.json`. The downloaded-MST
version list is `deploy/system/version_info.json` (169 tables). Per-mission battle data is
`deploy/battle/<id>.json`. See `GAP_ANALYSIS.md` for the full gap table.

## Phasing design (summary)

1. **Login-time tables** (`deploy/system/*.json`: summons, banners, daily dungeons, campaigns): one folder per phase, plus a `phase` config switch.
2. **Downloaded MSTs** (`F_UNIT_MST`, `F_MISSION_MST`, skills, evos…): serve phase-filtered copies and bump their versions in `version_info.json`, so the client re-downloads them. Reading is solved by `mst-decoded.zip`. Still to do: **re-encrypting** to the `.dat` format, and a filter that keeps references consistent (unit -> skills/LS/ES/evo mats/AI).
3. **Feature flags** (`deploy/system/features.json`: `raid`, `colosseum_enable`, `guild`, `frontierhunter`, `randall`, `challenge_arena`, …; also `F_FUNCTION_RELEASE_MST` and the `feature_gate` field). **Unverified** whether the client hides menus cleanly. Test one by one.
4. **Server-side enforcement**: handlers reject out-of-phase IDs.

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
  instead of fixed sleeps, taps the title until the login shows up, and presses Back on the
  Google Play Games prompt (BlueStacks is now signed in to Google, so it rarely appears).
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
  leaves ~970 of the 1,292 phase-1 missions without enemy data (the whole of Cordelica
  onwards is thin), which needs another source (wiki enemy lists, JP datamine). Enemy skills
  (`skills` in the captures) are not converted yet (warned).
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
  app data (`python tools/dev.py client --fresh`; the terms / phone-permission taps of the fresh
  flow are timing-sensitive, redo them by hand if it stalls).
- `rCB7ZI8x` **UpdateEventInfo** (key `L1o4eGbi`, URL `/actionSymbol/UEt5bqp4.php`,
  `gimuserver/gme/UpdateEventInfo.cpp`): `HomeScene2::updateEvent` sends it (async, login info +
  signal key) once the player has sat on the home screen for more than 300 s
  (`UpdateEventInfo::isNeedUpdate`). It was unregistered: "Unsupported request: rCB7ZI8x" popped
  up after the 20-minute download and the client restarted on OK. Stub answering `{}`.
- Town records on the wire (from `MyTownTopScene`): `eip3MS5L` drop_item_info is
  `itemId:count[:zel[:karma]],...` indexed by `count - tap_cnt`, read only when `mDaE3t6A` tap_cnt
  >= 1 (collect animation, `TownUpdateRequest` queued); `qA7M9EjP` startdate is never read by the
  client. Town locations are "open" when `HSRhkf70` need_mission_id is `0` or cleared.
- Where things are in the client: the Units tab has no item menu; the home MENU has only the
  guides; item screens (`ItemTopScene`, scene reached from `GameScene::getGameScene`) are in the
  village. The Randall facility map is `RandallTopScene` (home icon next to MENU).

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

## Next steps (in order)

1. Android patch: **done** (script, APK; home, Summon with a working Honor/Rare summon, Unit and Town browse without requests). Quests play end to end with the 2014 battle data (see "Quests play end to end"); next on the client side: the write operations (DeckEdit, fusion, sell) and item pickups once persistence exists, enemy skills in `build_battles.py`.
2. ~~Fork decompfrontier/server into bfanew~~ **done 2026-10-30** (`bfanew/server`, subtree with history; first fix: Android `6K7LToCD` key + tolerant JSON reads). Still to do: the `phase` config + `phases/NN/` folders for login-time tables.
3. MST tooling: decode/encode `.dat` (start from `mst-decoded.zip` and `BfCrypt.cpp`), plus the phase filter driven by `data/*_timeline.json`.
4. Core loop: ~~accounts and persistence → unit box → quests and rewards → summon~~ **done 2026-10-30** (see "Persistence"); ~~the warehouse / items~~ **done 2026-10-30** (see "Items / warehouse"; items used in battle still unverified on the client); ~~the village crash~~ **solved 2026-10-30** (content archive gap, see "Village crash"); ItemSell verified on the Sell Items screen; next: item synthesis (`ItemMix` `4P5GELTF`, key `AFqKIJ8Z4mHPB9xg`), energy purchase.
5. Test feature flags system by system.

## Conventions

- Don't modify the `decompfrontier/*` clones at all any more; all server work happens in `bfanew/server`. To pull upstream later: `git subtree pull --prefix=server ../decompfrontier/server main` (and the same for `server/packet-generator`).
- Published at https://github.com/hyrasch/bfanew (public, AGPL-3.0). Push `main` after each milestone (`git push`). Never commit the APK, assets, keystore or Revive's build.
- Git identity: `Sylvain Franco <sylvain.franco@outlook.fr>`. End commit messages with:
  ```
  Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
  ```
- Non-commercial fan project. Server code stays AGPL-3.0, so source must be offered to anyone using a public server. The APK, art and data belong to Alim/gumi.
