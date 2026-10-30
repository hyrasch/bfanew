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

Login handshake, encryption (SREE/GME AES) and static serving work. There are 11 GME handlers, several of them stubs. **Every player is the same hard-coded user** (`12345678`, "OfflineMod!", one fake unit). The account-creation SQL is `#if 0`, and the DB has a single unused `userinfo` table. **Quests, rewards, summons, fusion/evolution, items, town, arena, gifts and friends are not implemented.** Handler registry: `gimuserver/gme/GmeControllerHandlers.cpp` (`REGISTER(id, func, aesKey)`). Login-time tables are loaded in `gimuserver/drogon/ServerCache.cpp` from `deploy/system/*.json`. The downloaded-MST version list is `deploy/system/version_info.json` (169 tables with version numbers). See `GAP_ANALYSIS.md` for the full gap table.

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
python tools/dev.py summon [--rare] [--multi]   # from the Summon scene: summon, open the door (~25 s)
python tools/dev.py shot name             # screenshot -> ..uild\shots
ame.png (+ window frame)
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
- Known data gap: tapping a quest banner shows "Cost limit exceeded" because the stub team
  has no cost limit (`UserTeamInfo.deck_cost` / level table); harmless until quests.
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

1. Android patch: **done** (script, APK; home screen, Summon scene with the two launch banners, a working Honor/Rare summon). Next on the client side: follow the request log through Unit, Town, Quest; summons need a real pool/rates and persistence (step 4).
2. ~~Fork decompfrontier/server into bfanew~~ **done 2026-10-30** (`bfanew/server`, subtree with history; first fix: Android `6K7LToCD` key + tolerant JSON reads). Still to do: the `phase` config + `phases/NN/` folders for login-time tables.
3. MST tooling: decode/encode `.dat` (start from `mst-decoded.zip` and `BfCrypt.cpp`), plus the phase filter driven by `data/*_timeline.json`.
4. Core loop: accounts and persistence → unit box → quests and rewards → summon → fusion/evolution.
5. Test feature flags system by system.

## Conventions

- Don't modify the `decompfrontier/*` clones at all any more; all server work happens in `bfanew/server`. To pull upstream later: `git subtree pull --prefix=server ../decompfrontier/server main` (and the same for `server/packet-generator`).
- Published at https://github.com/hyrasch/bfanew (public, AGPL-3.0). Push `main` after each milestone (`git push`). Never commit the APK, assets, keystore or Revive's build.
- Git identity: `Sylvain Franco <sylvain.franco@outlook.fr>`. End commit messages with:
  ```
  Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
  ```
- Non-commercial fan project. Server code stays AGPL-3.0, so source must be offered to anyone using a public server. The APK, art and data belong to Alim/gumi.
