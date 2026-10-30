# bfanew: gap analysis

Goal: a Brave Frontier Global private server that is **faithful** (original client, original data, like Revive) and releases content **in phases** (like Reborn), instead of opening the final 2022 game all at once.

Written 2026-10-30 after reading `decompfrontier/server` (last commit 2026-06-15), `packet-generator` (2026-05-12), `offline-proxy` (2026-09-22), the project docs, and the history of the community datamine `cheahjs/bravefrontier_data`.

## Verdict

**Feasible, and phased content fits the way the game is built.** Almost everything you would want to drip-feed (units, quests, summons, events, systems) is *data the server sends or serves*. A phase is a filter on that data.

What makes this a long project is not the gating. **The decompfrontier server is an early skeleton.** It logs in, shows the home screen, and does very little after that. Nothing about a player is saved, and the core loop (quest, rewards, summon, fusion) is not written yet. Revive has built that part on top of decompfrontier, but its code isn't public as far as I can tell.

Realistic estimate: a playable phase 1 (story through the original lands, summons, fusion/evolution, arena) is **several months of steady evenings** for one or two people. Most of that is the core loop, not the phasing.

## What's in `D:\.Code\bravefrontier`

| Folder | What it is | Notes |
|---|---|---|
| `decompfrontier/server` | C++ (Drogon) server emulator for client v2.19.6.0 | AGPL-3.0. The base to fork. |
| `decompfrontier/packet-generator` | Packet and MST definitions (`.kdl`) that generate C++ structs | Server git submodule. This is where packet field meanings are documented. |
| `decompfrontier/offline-proxy` | Windows DLL that redirects the PC/WinRT client to a local server | **Windows only.** No Android equivalent exists. |
| `decompfrontier/offline-diff` | Early patch scaffolding | Two commits from 2023, basically empty. |
| `decompfrontier/docs` | Project website source | Architecture, TODO and setup guides. |
| `bfanew` | Our repo | This file plus `data/`. |

Not copied: `decompfrontier/client` (a client decompilation, 180 MB, not needed for server work).

## Where the server stands today

**It works:**
- The login handshake (DLS and gumi-live), the request encryption (AES), and the encrypted-request dispatch.
- Serving static files (assets and MST files) from `deploy/game_content`.
- 11 handlers: `Initialize`, `UserInfo`, `HomeInfo`, `UpdateInfoLight`, `BadgeInfo`, `ControlCenterEnter` (Brave slots), `ChallengeArenaResetInfo`, `DeckEdit`, `FriendGet`, `GatchaList`, `MissionStart`. The last four are stubs.
- 45 server-side master tables loaded from `deploy/system/*.json` (summons, banners, town, trophies, login campaign, daily tasks, Frontier Hunter, etc.).

**It's stubbed or hard-coded:**
- **Every player is the same player.** `Initialize`/`UserInfo` return a fixed user `12345678` named "OfflineMod!" with one fake unit and invented stats. The account-creation SQL exists but is disabled (`#if 0`).
- **The database has a single `userinfo` table that nothing reads or writes.** There are no units, items or progress tables.
- Their own TODO list marks all of these as not started: unit fusion, evolution, selling, Omni, spheres, items, town/crafting, quests, friends, gifts, arena, raids, Colosseum, tutorial and account creation.

**Assets:** the docs point to a `21900.zip` on Google Drive (the `content` and `mst` folders). A list of known missing files is in `docs/source/pages/Status/missing-asset.rst`. These are mostly 2018–2022 summon banners, which phase 1 won't need anyway.

## The client side (Android)

- The documented development route is the **Windows/WinRT client plus offline-proxy**. That's the easiest way to debug, since the proxy logs every request.
- **For Android, decompfrontier doesn't ship a working redirect.** Its Android bootstrap is an empty `// TODO`. The older emufrontier README describes the approach: hex-patch the server URLs in `libgame.so` (`https://` → `http://`, plus the hostnames), then re-sign the APK. Revive ships an Android APK, so this is solved in practice. We would redo it ourselves or ask them how.
- **You need the v2.19.6.0 APK.** It isn't in the repos. If you still have it, put it in `bravefrontier/client/`.

## How phases would work

There are four ways to hold content back, from easiest to riskiest:

1. **Tables the server sends on login** (summon pools, banners, daily-dungeon list, login rewards, events). These are already plain JSON in `deploy/system`. One folder per phase (`phases/01/…`) and a config switch. **Easy.**
2. **Master files the client downloads** (`F_UNIT_MST`, `F_MISSION_MST`, `F_SKILL_MST`, evolutions, items… 169 tables listed in `version_info.json`, each with a version number). The client re-downloads a table when its version changes. So a phase serves a **filtered copy** of each table and bumps the version. This needs:
   - tooling to decode and re-encode those files (format to confirm once the asset zip is local; bfdb and Revive both read them, so it's known territory);
   - a filter that keeps references consistent. If a unit is in, its skills, leader skill, evolution materials and AI have to be in too. **Medium.**
3. **Feature flags** (`features.json`: `raid`, `colosseum_enable`, `guild`, `frontierhunter`, `randall`, `challenge_arena`, `bf_campaign_grand_quest`, …). These switch whole systems off. There's also an `F_FUNCTION_RELEASE_MST` table and a `feature_gate` field that look like they unlock menus. **This is the open question:** whether the final client hides the menus cleanly or crashes on empty data has to be tested system by system. If one misbehaves, the fallback is patching `libgame.so`, which is slow.
4. **Server-side enforcement.** Handlers refuse unit, quest or summon IDs that aren't in the current phase, so a modified client can't skip ahead. **Easy once the handlers exist.**

### Where the release order comes from

`cheahjs/bravefrontier_data` has 3,969 commits of Global datamines from **Aug 2014 to Nov 2021**. Replaying its history month by month gives the month each unit and quest first appeared:

- `data/unit_timeline.json`: 2,940 unit IDs (2014: 1,421 · 2015: 473 · 2016: 407 · 2017: 321 · 2018: 111 · 2019: 125 · 2020: 72 · 2021: 10)
- `data/mission_timeline.json`: 3,746 mission IDs (2014: 1,349 · 2015: 508 · 2016: 442 · 2017: 278 · 2018: 221 · 2019: 247 · 2020: 536 · 2021: 163)

Land order from the same data: Mistral, Agni, Cordelica, Palmyna, Lizeria and the Vortex (already present in Aug 2014), then Vriksha (Oct 2014), Atharva (Apr 2015), Bariura (May 2015), Celgrad, Lem, Beiorg, Wulgee, Rakshult and Zamburg (by Jan 2016). After that, lands are stored as numeric IDs (Chapter 2 onward).

**Caveats:**
- Global launched in Nov 2013. The first ~9 months are all lumped into "2014-09", so phase 1 can't be split finer from this source.
- The snapshots keep each unit's stats **as they were at that date**, so historical balance can be recovered. But only for the fields bfdb parsed, not the raw tables, and I haven't yet measured how often units were rebalanced.
- From 2016 onward, Global also got some JP content out of order. The timeline reflects Global, which is what you want.

## Gap list for a phase-1 launch

Phase 1 = the original six lands and the basic Vortex, units up to the 2014 roster, rare summon, arena, town.

| Area | Status in decompfrontier | Effort |
|---|---|---|
| Build and run the server locally | Works (VS 2022 or GCC/Ninja + vcpkg) | S |
| Real accounts and saving them | Disabled stub | M |
| Unit box, party edit, fusion, evolution, selling | Missing (one fake unit) | L |
| Items, inventory, spheres | Missing | M |
| Quests: start, finish, rewards, energy, drops | `MissionStart` stub only; no finish handler | L |
| Rare summon and rates | `GatchaList` stub; no summon handler | M |
| Friends and support units | Stub (could use fake NPC friends at first) | M |
| Town: facilities, crafting, Bazaar | Missing | M |
| Arena | Missing | M |
| Vortex events and keys | Missing | M |
| Gift box | Missing | S |
| Tutorial | Skipped by a hack | S–M |
| Tooling to decode and re-encode MST files | Missing | M |
| Phase filter plus reference checks | Missing (this is ours) | M |
| Android redirect patch | Missing here; Revive solved it | M |
| Assets | Google Drive zip; known gaps are late-era only | S |

S = days, M = 1–3 weeks, L = a month or more, for one person part-time.

Not needed for phase 1: raids, Frontier Hunter, Colosseum, guilds, Summoner's Arc, Omni, Dual BB, Grand Quests and the Randall capital. All of those are *later* phases, which is the point of the project.

## Suggested order

0. **Get decompfrontier running locally with the Windows client** (their tutorial). This proves the toolchain and gives you request logs.
1. **Fork the server into `bfanew`**, add a `phase` setting and `phases/NN/` folders for the server-sent tables (lever 1). This is quick and shows the idea working end to end.
2. **Write the MST decode/encode and phase-filter tooling** (lever 2), using `data/*_timeline.json` to assign every ID to a phase.
3. **Build the core loop**, in this order: accounts and saving, then the unit box, then quests and rewards, then summons, then fusion/evolution.
4. **Test the feature flags** (lever 3) one system at a time, and record which ones hide cleanly.
5. **Android patch**, then let a few testers in.

**Shortcut worth a message:** Revive built on AGPL code. If they run a public server, the AGPL normally requires them to share their modified source with its users. Asking for it, or proposing a phased server on their stack, could skip most of step 3.

## What I need from you

- Download `21900.zip` (the link is in `decompfrontier/docs/source/pages/Tutorial/dev-server.rst`) into `bravefrontier/assets/`. Neither this session nor your machine's shell could reach Google Drive.
- The v2.19.6.0 APK, if you have it. The WinRT `.appx` link is in the same docs, for PC testing.
- Whether you have Visual Studio 2022 on this PC, or prefer WSL/Linux for building.
- Your phase-1 cutoff. For example: "everything Global had by end of Oct 2014" (adds Vriksha), or "before the first 6★s".

## Legal notes

The server code is AGPL-3.0, so if bfanew is ever run for other people, its source has to be offered to them. The APK, art and data belong to Alim and gumi. Keep it non-commercial, and accept that a takedown is possible.
