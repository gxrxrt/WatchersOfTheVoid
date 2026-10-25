# Fix plan — Watchers of the Void stability fork

Repo: `C:\Users\garar\Desktop\dev\votv_fix\WotV` (git clone of ElvardFIC/WatchersOfTheVoid, branch `fix/coop-stability`).
Build: `bash /c/Users/garar/Desktop/dev/votv_fix/tools/build.sh` (serialized via lock; prints errors + `BUILD EXIT n`).
Output DLL: `WotV/build/votv-coop/Release/main.dll`. Deploy target: Thunderstore profile `...\shimloader\mod\elvardfic-watchersofthevoid\dlls\main.dll`.

Investigation reports: `notes/01..06-*.md`.

## Done (orchestrator)
- #3 laptop stuck on tab (Viewer = server-hash page): `shared_screen::Apply` no longer fails on rows missing locally + re-walks after a tab switch; operator repaint latch bounded to 3 tries in `shared_screen_sync` and `laptop_ui_sync`.
- #4 transformers: `power_grid.cpp` watched `powerGenerator_C` (merge typo) — real class `generator_C`. All generator watches were dead; client repairs ran locally and were reverted by host rows.

## Work streams (one agent each, disjoint files)
| Stream | Bugs | Owner files (others: read-only, ask orchestrator) |
|---|---|---|
| PROPS | #1, #5 | `src/coop/props/**`, `src/coop/element/mirror_defer.cpp`, `src/coop/dispatch/event_dispatch_entity.cpp` + headers |
| EVENTS | #2 | `src/coop/creatures/npc_world_enum.cpp`, `src/coop/world/event_*`, `world_actor_*`, `spawn_authority.cpp`, `weather_event_births.cpp`, `npc_mirror.cpp`, `sdk_profile_names.h` (allowlists only) |
| INTERACT | #4 | `src/coop/interactables/door_verb_intent.cpp`, `kitchen_sync.cpp`, `src/ue_wrap/devices/cremator.*`, `src/coop/world/credit_intent.cpp`, `power_*.cpp` |
| DRONE | #6 | `src/coop/interactables/drone_*`, `src/ue_wrap/devices/drone*`, new `drone_watchdog.*`, `toggle_verbs.cpp` (drone rows only) |
| UI/FUN (orchestrator) | QoL, Claude figurine | `src/ui/**`, new files |

Shared hot files: `include/coop/net/protocol.h` (append new kinds at the END of enums only; do NOT bump kProtocolVersion — orchestrator bumps once), `src/coop/session/subsystems.cpp` (add your Tick/Install call lines only), `sources.cmake` (append your new files).

## Rules
- Follow docs/coop-sync-doctrine.md: host authority, intents from clients, no receive-side gates that lose state.
- Smallest change that fixes the root cause; comment density like surrounding code.
- Every new behavior logs once on its decision edge (UE_LOGI/UE_LOGW) so a 2-player test can confirm it.
- Must compile via tools/build.sh. Do not commit; orchestrator commits.
- Write a short changelog of what you changed (file:line, why) to `notes/impl-<stream>.md`.

## Status at pause (usage limit)
- Committed on fix/coop-stability: laptop tab fix (#3), generator class typo (#4 transformers), GNS include build fix. Release build OK.
- Implementation agents (props, events, interact, drone, l10n) were STOPPED mid-work: any uncommitted changes in `git status` are partial — review or `git restore` them before building.
- Next: re-run the 5 streams from this plan; session pane (F1 > Network > Session: invite code, recent servers, host resync via subsystems::ConnectReplayForSlot); Claude figurine (runtime plush + procedural terracotta star, key prefix `claude_fig_`); bump kProtocolVersion; deploy main.dll to Thunderstore profile (back up original); push fork.
