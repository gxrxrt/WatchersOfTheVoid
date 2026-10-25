# Shared context for investigation agents

Project: fix the multiplayer mod "Watchers of the Void" (fork of Multivoid) for Voices of the Void Alpha 0.9.0n (UE 4.27).

Paths (Windows, Git Bash: /c/Users/garar/...):
- Mod source (zip of GitHub main): C:\Users\garar\Desktop\dev\votv_fix\WatchersOfTheVoid-main
  - C++ DLL source: src/votv-coop/{include,src}/  (coop/props, coop/world, coop/interactables, coop/net, ue_wrap/devices, ue_wrap/desk ...)
  - Docs: docs/*.md (README.md is the index; status.md, props.md, events-and-weather.md, devices.md, signals.md, base-computer.md, coop-sync-doctrine.md, coop-entity-expression-map.md)
  - Maintainer notes: agent/TODO.md, agent/TESTLOG.md, agent/ENTITY_COVERAGE.md, agent/SYNC_2157_FIXES.md, agent/FORK_AUDIT_20261022.md
  - Game reference dumps (if any): reference/
- Game install: C:\Users\garar\Desktop\dev\votv_fix\a09n\WindowsNoEditor (VotV-WindowsNoEditor.pak 8 GB; do NOT try to unpack it)
- Runtime logs: a09n\WindowsNoEditor\VotV\Binaries\Win64\multivoid.log, UE4SS.log
- Installed mod (Thunderstore profile): C:\Users\garar\AppData\Roaming\Thunderstore Mod Manager\DataFolder\VotV\profiles\Default\shimloader\mod\elvardfic-watchersofthevoid (multivoid.ini lives here)

Architecture in one breath: a native DLL loaded by UE4SS (via Thunderstore shimloader). It hooks ProcessEvent, the Blueprint VM script loop ("script_gate"), AActor::EndPlay, and individual UFunction::Func pointers. Networking is GameNetworkingSockets. Host is authoritative for most world state; clients send "intents/ops" for actions. Each peer runs its own local simulation.

User-reported bugs (from Russian TZ):
1. Props/physics/collision desync; some items invisible to the other player until someone interacts with them.
2. Event desync: event runs on host, nothing appears on client.
3. In-game computer freezes on the "Viewer" tab (where hash codes scroll) and won't switch to other tabs.
4. Clients cannot interact with: the furnace/stove in the basement, repairing transformers, the mining farm. Client actions do nothing; host must do everything.
5. Frequent item duplication or full disappearance.
6. Drone sometimes bugs and doesn't fly back.

Rules for investigators: READ-ONLY on source. Write your report ONLY to the notes file you are told. Cite file:line for every claim. Separate [V] verified-from-code claims from [?] hypotheses. Propose concrete fixes (which function, what change), ranked by confidence and blast radius. Keep reports under ~400 lines.
