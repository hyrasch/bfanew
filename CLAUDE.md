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
SDK at `D:\.Codendroid-sdk` with `cmdline-tools/latest` and `build-tools/34.0.0`
(`zipalign`, `apksigner`). Python packages: `cryptography`, `pyaxmlparser`, `capstone`, `pyelftools`.

**Patch script:** `tools/patch_apk.py` applies the byte patches above to both `libgame.so`,
re-zips (signature files dropped, compression kept per entry), then runs `zipalign` and
`apksigner` (v1+v2, self-signed key `build/bfanew-debug.keystore`, created on first run).
It checks the input SHA-256 and every original byte before patching. The manifest is NOT
changed (no `usesCleartextTraffic`): libcurl ignores the cleartext policy; revisit if the
WebView pages (news/notice) matter.

```powershell
$env:JAVA_HOME = "C:\Program Files\Microsoft\jdk-17.0.21.21-hotspot"; $env:Path = "$env:JAVA_HOMEin;$env:Path"
cd D:\.Coderavefrontierfanew
python tools/patch_apk.py --apk ..rave-frontier-2-19-6-0.apk --host 192.168.1.14 --sdk D:\.Codendroid-sdk --out ..uild
# -> ..uildf-192.168.1.14-9960.apk  (verified: v1 true, v2 true)
adb install -r ..uildf-192.168.1.14-9960.apk
```

**Device:** a real device, the user's **Ayn Thor** (Android handheld, arm64), same as
Revive. No emulator. Firewall rule for TCP 9960 inbound was added on 2026-10-30.

**Server side done:** `deploy/config.json` now has two listeners, `192.168.1.14:9960`
first (what `/dls` advertises) and `127.0.0.1:9960` second. The Ethernet profile is
"Public", so Windows Firewall will drop inbound 9960 until a rule is added (admin):
`netsh advfirewall firewall add rule name="gimuserver 9960" dir=in action=allow protocol=TCP localport=9960`.
The old single-listener config is kept as `deploy/config.json.bak-127only`.

## Next steps (in order)

1. Android patch: analysis and `tools/patch_apk.py` done, APK built. Still to do: install on the Ayn Thor, read the server request log (`deploy/log/`, TRACE level) to see how far the client gets, stub what is missing. Done = client reaches Initialize/UserInfo on our server.
2. Fork decompfrontier/server into bfanew (keep AGPL, keep upstream history if possible), and add the `phase` config + `phases/NN/` folders for login-time tables.
3. MST tooling: decode/encode `.dat` (start from `mst-decoded.zip` and `BfCrypt.cpp`), plus the phase filter driven by `data/*_timeline.json`.
4. Core loop: accounts and persistence → unit box → quests and rewards → summon → fusion/evolution.
5. Test feature flags system by system.

## Conventions

- Don't modify the `decompfrontier/*` clones beyond local build files. Real changes go into bfanew (step 2 forks the server there).
- Git identity: `Sylvain Franco <sylvain.franco@outlook.fr>`. End commit messages with:
  ```
  Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
  ```
- Non-commercial fan project. Server code stays AGPL-3.0, so source must be offered to anyone using a public server. The APK, art and data belong to Alim/gumi.
