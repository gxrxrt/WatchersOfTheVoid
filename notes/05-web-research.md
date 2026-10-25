# 05 - Web research (GitHub issues, Thunderstore, VotV wiki, modding)

Researched 2026-10-26. Sources are linked inline. [V] = read directly from the source; [?] = my inference.
GitHub data pulled via the REST API (anonymous; rate limit hit near the end, so commit lists are the latest 100 per repo plus path-filtered histories).

---

## 0. Repo facts

| Repo | Issues | Notes |
|---|---|---|
| [ElvardFIC/WatchersOfTheVoid](https://github.com/ElvardFIC/WatchersOfTheVoid) | Issues tab **disabled**; only 3 PRs (#1-#3). #2/#3 are bug reports filed as PRs because the reporter "can't create an issue". | Fork; desc: "Co-op for VotV 0.9.0n: 2-32 players ... Every version is a test version". Last push 2026-10-25 (0.9.2160). |
| [VOTV-MP/Multivoid](https://github.com/VOTV-MP/Multivoid) (upstream, maintainer `pelmentor`) | 54 issues/PRs (#5-#61) | Very active; most fixes land as cherry-picks by pelmentor. |
| [AquaFishhh/MultivoidKindaPlus](https://github.com/AquaFishhh/MultivoidKindaPlus) | 0 issues | Last commit 2026-10-11; b2142 snapshot was imported into the fork in [852d667d6](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/852d667d694e9ba8d632d783125e49cffa7625f6). |

GitHub commit search does not index these forks (returned 0 for every term), so the "commits touching area X" lists below come from `commits?path=...` queries.

---

## 1. User reports matched to the 6 TZ bugs

### Bug 1: props/physics desync, invisible props until interacted with
| Link | Symptom | Maintainer response / fix |
|---|---|---|
| [ElvardFIC PR #3 "Bugs №2"](https://github.com/ElvardFIC/WatchersOfTheVoid/pull/3) (mega-maks2011, 2026-10-23, RU+EN) | Bug 2: "Players sometimes see a visual dupe of an item; this item is a complete copy." Bug 1: shovel holes not visible to others. Bug 3: opened canned food refills for the opener (no sync). | Closed without comment. The 0.9.2160 changelog says "Implement shared shovel holes, can-portion guards" ([Thunderstore changelog](https://thunderstore.io/c/voices-of-the-void/p/ElvardFIC/WatchersOfTheVoid/changelog/)). Visual dupe is not mentioned as fixed. |
| [Multivoid #14](https://github.com/VOTV-MP/Multivoid/issues/14) | "Sometimes clients see duplicated objects that only client see but not host"; clients grab objects through walls. | Open, no reply. |
| [Multivoid #13](https://github.com/VOTV-MP/Multivoid/issues/13) | "sweeping not updating the physics of trash piles", general desync; host 60 fps vs client 20 fps. | Open, no maintainer reply. |
| [Multivoid #56](https://github.com/VOTV-MP/Multivoid/issues/56) | Map poster changes into a different poster when the other player picks it up (prop record/variant desync). | Open. |
| [Multivoid #55](https://github.com/VOTV-MP/Multivoid/issues/55) | Client picked up a drive box into inventory (weight shown ~300 instead of 516), destroying its discs. | Open. |
| [Multivoid #49](https://github.com/VOTV-MP/Multivoid/issues/49) | 3D-printed objects never sync until re-save/relaunch. | Open. |
| [Multivoid PR #36](https://github.com/VOTV-MP/Multivoid/pull/36) (merged-by-cherry-pick) | Maintainer diagnosis: `prop_save_data` sent a prop's save record **only on birth paths**, so in-place state changes never crossed: "a drive put in stayed invisible until somebody nudged the box", and taking one out left a duplicate on the guest. Fix: watch each class's `upd()` refresh verb (drive box, tape reel case). | Directly the "invisible until interacted" mechanism for container-like props. |
| [Multivoid PR #39](https://github.com/VOTV-MP/Multivoid/pull/39) | Runtime births on the host (ejected discs) reached peers at class default; a client re-publishing the blank wiped the host's disc. Fix: publish the record a tick after birth from the spawn-seam drain. | Same family: record-after-birth gap. |
| Changelog 0.9.2146 ([Thunderstore](https://thunderstore.io/c/voices-of-the-void/p/ElvardFIC/WatchersOfTheVoid/changelog/)) | "Fixed a missing item-state broadcast when the host registered an object before its saved charge/uses were restored ... one cause of batteries and other consumables appearing full on the other peer." | |
| Changelog 0.9.2122 | Maintainer admits: "This does not add general synchronization for every loose physics object in the map." Only held/gravity-gun/stood-on props are streamed. | [V] Explains "physics desync" for untouched props: loose props simulate locally on each peer. |

### Bug 2: event runs on host, nothing on client
| Link | Symptom | Response |
|---|---|---|
| [ElvardFIC PR #3](https://github.com/ElvardFIC/WatchersOfTheVoid/pull/3) Bug 6.1/6.2 | Ariral warp-arrow event seen only by reporter; players far from base get teleported to it. | No reply. 0.9.2160 changelog: "current Ariral ship presentation/join state". |
| [Multivoid #42](https://github.com/VOTV-MP/Multivoid/issues/42) (+ comment by ThatDarki 2026-10-24, b208) | "Paranoia events and just general events seem to be desynced" (fog + entity noise on one peer only); nightmares host-only; abduction blacks the screen then fails to teleport, after which held items are deleted on use; "The entity is invisible to the abductee and other players around, but audio and fog effects events are triggered correctly." | Open, no maintainer reply. |
| Fork changelog (0.9.2120 / 2124 / 2150 / 2155) | Design stated: "The host runs them. A client does not roll its own list"; location events pull everyone to the triggering player; "Other creature/event paths remain incomplete"; 2155: "The complete eight-category event request, the full repeatable random-event list, and real-peer event ... acceptance remain open." | [V] Maintainers acknowledge partial event coverage. |
| Commit [bf276d14c](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/bf276d14c4d30dc1ba95ea9ea184f7d9e89dedef) (AquaFishhh, 2026-10-19) | Added `event_cue_sync.cpp`, `world_sound_sync.cpp`, touched `event_fire_sync.cpp`: "Publish native world sound cues". | |
| Upstream [b8bf2fe85](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/b8bf2fe85) | "the story event's trap reaches whichever copy is driven" (ATV-related). | |

### Bug 3: computer stuck on "Viewer" tab
No GitHub issue describes the exact freeze. Closest evidence:
| Link | Relevance |
|---|---|
| [Multivoid #47](https://github.com/VOTV-MP/Multivoid/issues/47) + [PR #53](https://github.com/VOTV-MP/Multivoid/pull/53) | Hash-code data broken for non-hosts ("Hash Codes from others register as 0x000000 ... dead buffer space", `INVALID_DATA_BUFFER`). pelmentor diagnosis (2026-10-25): floppy slot / floppy box / laptop lanes sent rows through the chat encoder, which drops control chars, so the multi-line server-data row (hash code included) "reached a client on one line and did not parse there". Fixes: `334b26f10` (contributor), `fd9fcef86` (one text codec both ways, row read whole before apply), `0943b6d22`, drive-rack `cad28c956`, `307aaea8d`, `db1679c08`, `d800f3b6b`. The local zip's `docs/credits.md:41` credits this fix, so it is in the fork tree. |
| Commit [bf276d14c](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/bf276d14c4d30dc1ba95ea9ea184f7d9e89dedef) | "Share PC and laptop pages and ordered shop carts with operator handover" -> new `coop/interactables/laptop_ui_sync.cpp`, `ue_wrap/devices/laptop_ui.cpp`, `device_occupancy.cpp`. |
| Changelog 0.9.2156 | "Prevent shared-screen repaint from restoring the stationary PC's loading page after startup ... actual startup and repeated power cycles still need host/client acceptance." and "Add shared computer display/operator handoff and readiness recovery." 0.9.2150 lists "shared computer display" as an open report. 0.9.2160: "Improve concurrent console ownership ... native display callback lifetime." |
| Upstream [c930d6aa5](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/c930d6aa5) | "laptop_widget reaches the laptop interface's tab switcher". |
| [Multivoid #61](https://github.com/VOTV-MP/Multivoid/issues/61) | Player who faints while at the signal terminal stays "using" it for everyone until they leave (stale occupancy). Similar class of bug: stale operator lock. |
| [ElvardFIC PR #3](https://github.com/ElvardFIC/WatchersOfTheVoid/pull/3) Bug 4 | "When I'm on the satellite and open the console, other players can't open other consoles anywhere on the map" -> global (not per-device) console lock. |

[?] Hypothesis worth checking in code: the fork's shared-page mirror (`laptop_ui_sync`) repaints the PC to the operator's/host's current page; a non-operator pressing another tab is overwritten on the next repaint, or a stale operator claim (like #61) never hands over, so the screen looks "frozen on Viewer". The Viewer is the tab that renders the hash-data blocks (see section 3), which is why it is the one users notice.

### Bug 4: clients cannot use cremator/furnace, transformers, mining rig
| Link | Symptom | Response |
|---|---|---|
| [Multivoid #43](https://github.com/VOTV-MP/Multivoid/issues/43) | GPU mining rig not synced: client sees an empty frame; when the client used the display to collect points, "everything from the rack disappeared". | Open, no reply. |
| [Multivoid #60](https://github.com/VOTV-MP/Multivoid/issues/60) (2026-10-25) | Transformer upgrades installed by a non-host vanished after that player repaired the transformer. | Open. |
| [Multivoid #59](https://github.com/VOTV-MP/Multivoid/issues/59) | Radar tower repaired by a non-host does not count; host must repair. | Open. |
| [Multivoid #28](https://github.com/VOTV-MP/Multivoid/issues/28) | "Oven being fixed is client side." | Open. |
| [Multivoid #14](https://github.com/VOTV-MP/Multivoid/issues/14) item 4 | Client turning off the generator kills lights locally but host sees nothing. | Open. |
| Fork changelog 0.9.2124/2127 | Transformer repair door (yellow button) shared through the power-grid channel; "Transformer puzzles and the three coordinate radar towers already had synchronization in upstream MultiVoid". 2155/2160: "Paid client repair, resource transactions ... remain open"; "ordinary paid wall repair remains disabled". | [V] Client repairs are explicitly unfinished. |
| [Multivoid PR #57](https://github.com/VOTV-MP/Multivoid/pull/57) (open) | Adds an admin "send player to the cremator" action that locks the cremator door/lever on clients. Shows the cremator has no general client-use sync yet. | |
| Upstream commits [1bb1d1ace](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/1bb1d1ace) "the repair reward's inputs are bounded and its record is shared"; [6282ea46e](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/6282ea46e) "a client's ping is rolled on the host's desk". | Pattern used elsewhere: client press -> host performs. | |
| Pattern PRs [#38](https://github.com/VOTV-MP/Multivoid/pull/38), [#34](https://github.com/VOTV-MP/Multivoid/pull/34) | Maintainer pattern for "client action does nothing": "The client now refuses its own body at the script-body gate and sends the press" and host re-runs it. #38 explains why: level-referenced objects on a client are local mirrors whose tick is suppressed, so a client-local verb "reached a drone that cannot fly and told nobody". | Likely the same root cause for cremator/rig/transformer. |

### Bug 5: item duplication / disappearance
| Link | Symptom | Response |
|---|---|---|
| [Multivoid #42](https://github.com/VOTV-MP/Multivoid/issues/42) | Floppy duplicates after eject; drone bag duplicated when host grabs/places it, client gets a usable "fake bag". | Open. |
| [Multivoid #24](https://github.com/VOTV-MP/Multivoid/issues/24) | Ejected floppy disappears/unpickable; another peer's physics re-inserts it (slot overlap race). | Open. PR [#33](https://github.com/VOTV-MP/Multivoid/pull/33) fixed a related "drive stays frozen in the port" case (applied eject now runs `awakeUnfreeze`). |
| [Multivoid #22](https://github.com/VOTV-MP/Multivoid/issues/22) | "Items randomly disappearing from client and host inventories"; can't rejoin. | Open. |
| [Multivoid #13](https://github.com/VOTV-MP/Multivoid/issues/13) | Items vanish when picked up airborne; upgrade modules dupe; inventory wiped on rejoin. | Open. |
| [Multivoid #58](https://github.com/VOTV-MP/Multivoid/issues/58) | After loading ~2k weight of trash into the drone sack, most storage crates and their contents despawned. | Open. |
| [Multivoid #26](https://github.com/VOTV-MP/Multivoid/issues/26) | Client's food/sleep/inventory overwritten with host's on load-in. | Open. |
| [Multivoid #43](https://github.com/VOTV-MP/Multivoid/issues/43) | Rig contents vanish on client interaction. | Open. |
| [Multivoid PR #35](https://github.com/VOTV-MP/Multivoid/pull/35) | Upgrade purchase ran locally on client -> money duplication; fixed (host debits). | |
| Fork changelog 0.9.2141/2143 | "duplicate client props after R + left click" fixed with one placement request per actor. 2155: "Bench58 is now confirmed as an old rifle disappearing after placement by the host; its cause remains unverified." 2150 open reports: "ZIP ejection and disappearing workbench items". 2160: "Preserve exact inventory/container identities through checked restoration, refused sends, joining and proven local failed-extraction recovery." | |
| Upstream [fae4bd187](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/fae4bd187), [583f96864](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/583f96864) (2026-10-19) | "the join sweep spares the hand and what the player made" / "a spared birth is the actor recorded" -> the join sweep was deleting held/crafted items on join. | Disappearance on join. |

### Bug 6: drone does not fly back
| Link | Symptom | Response |
|---|---|---|
| [Multivoid #14](https://github.com/VOTV-MP/Multivoid/issues/14) item 6 | "Clients can't call a drone, only host." | Fixed by PR [#38](https://github.com/VOTV-MP/Multivoid/pull/38) (client console press forwarded to host). |
| [Multivoid #13](https://github.com/VOTV-MP/Multivoid/issues/13) item 5 | Drone UI not working until reload. | Open. |
| [Multivoid #59](https://github.com/VOTV-MP/Multivoid/issues/59) | Radar/radio tower repaired by client not counted. **Wiki: the drone only travels while the radio tower works and "leaves automatically once the tower is fixed".** | [?] If the tower is fixed only on the client's copy, host's drone never leaves = "drone doesn't fly back". Strong lead. |
| [Multivoid #42](https://github.com/VOTV-MP/Multivoid/issues/42) | Drone bag duplicated by host grab/place. **Wiki: "The drone cannot be dismissed if the sack is not attached."** | [?] A desynced/duplicated sack (detached on one peer) blocks dismissal. Second strong lead. |
| [Multivoid PR #50](https://github.com/VOTV-MP/Multivoid/pull/50) / commits [aac7e150f](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/aac7e150f), [72a302fcc](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/72a302fcc), [683fdc1e5](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/683fdc1e5) | Drone leave-mode (stay / leave after 5 min) synced through host; "the drone's leave timer clicks where it is pressed; v238". | |
| Upstream [fa320bf1f](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/fa320bf1f) "a client's drone rests where the host's does", [7da4bf3a2](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/7da4bf3a2) "a parked drone costs its host a few memory reads a second" | Client drone is a mirror whose flight tick is suppressed (PR #38). | |
| Fork changelog 0.9.2120 | "The scooter's map screen, the drone's bag, and the desk's rear panels still do not sync." 0.9.2133: "remaining ATV/drone tasks are still pending." | [V] Drone bag sync acknowledged as missing. |

### Other reports in the same PRs (not in TZ but related)
ElvardFIC [PR #2](https://github.com/ElvardFIC/WatchersOfTheVoid/pull/2): sleeping body model stays and stacks; ragdoll (C) falls through floor; host-bought sleeping bags not visible in client's drone menu. PR #3: second ATV sometimes does not spawn; time skips when only host sleeps (2160 added "all-player sleep voting").

---

## 2. Recent commits on main touching these areas (path-filtered, ElvardFIC fork)

Fork-only (ElvardFIC/AquaFishhh):
- [4b8f81c49](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/4b8f81c49) 2026-10-25 "[sync] Integrate upstream while preserving fork behavior" (touches props, world, drone_sync, devices, desk, interactables).
- [157fefd70](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/157fefd70) / [8cd4a68a0](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/8cd4a68a04a6ce8e81b0d59a6515fe876cedc056) 2026-10-25 release 0.9.2160 + notes (agent/TESTLOG.md, TODO.md, WORK.md only).
- [bf276d14c](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/bf276d14c4d30dc1ba95ea9ea184f7d9e89dedef) 2026-10-19 "Expand shared world and computer synchronization" (laptop_ui_sync, device_occupancy, event_cue_sync, world_sound_sync, event_fire_sync, placed_save_sync). Message: "Two-player gameplay verification remains outstanding."
- [852d667d6](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/852d667d694e9ba8d632d783125e49cffa7625f6) 2026-10-13 Import MultivoidKindaPlus b2142 (300 files; incl. drone_intent/state tests).

Upstream commits now in the fork (pelmentor unless noted):
- Props: [a39259354](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/a39259354) joiner announces only after save replay (10-24); [fae4bd187](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/fae4bd187), [583f96864](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/583f96864) join sweep spares hand/crafted items (10-19); [4d3a77742](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/4d3a77742) client's parked claim republished when it lands; [70bdaa0e5](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/70bdaa0e5) wire spawn retires a deferred destroy; [6ef1e4341](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/6ef1e4341) hand-view actor destroy is not a world prop destroy; [fbbcabef0](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/fbbcabef0) host judges each act by game conditions (10-18).
- Drone: [683fdc1e5](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/683fdc1e5), [72a302fcc](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/72a302fcc) (10-18), [aac7e150f](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/aac7e150f) arigalit (10-15), [bfc5bf5aa](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/bfc5bf5aa) "host-only kinds' client handlers lose their role checks" (10-11), [fa320bf1f](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/fa320bf1f) (09-25).
- Devices/desk: [c930d6aa5](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/c930d6aa5) laptop tab switcher (10-18); [3d03cb336](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/3d03cb336) desk-ping mirror never writes a running machine; [1bb1d1ace](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/1bb1d1ace) repair reward record shared (10-09); [b88fade8b](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/b88fade8b) drive row host cannot write leaves drive as-is; [7e8b7af35](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/7e8b7af35) download arm/reset are host's.
- World/events: most 10-18..10-24 commits are ATV/explosion/salt-rocket lanes; [b8bf2fe85](https://github.com/ElvardFIC/WatchersOfTheVoid/commit/b8bf2fe85) story-event trap.
- Hash/Viewer data (upstream, 10-25): `334b26f10`, `fd9fcef86`, `0943b6d22`, `cad28c956`, `ac91b8184`, `307aaea8d`, `db1679c08`, `d800f3b6b` (see PR #53 comment).

Nothing in the last ~100 commits per area mentions cremator/furnace, GPU rig/frame, or transformer upgrades: those remain unaddressed upstream and in the fork.

---

## 3. Thunderstore

- Package: [ElvardFIC/WatchersOfTheVoid](https://thunderstore.io/c/voices-of-the-void/p/ElvardFIC/WatchersOfTheVoid/) (created 2026-10-18, latest **0.9.2160** on 2026-10-25, DLL/network b2215, depends on `Thunderstore-unreal_shimloader-1.1.7`). ~2.4K downloads.
- Versions (date, downloads): 0.9.2160 (10-25, 21), 2158 (10-22, 849), 2157 (10-22, 116), 2156 (10-21, 262), 2155 (10-21, 115), 2154 (10-19, 526), 2153 (10-19, 81), 2152 (10-19, 215), 2151 (10-18, 173), 2150 (10-18, 111). The changelog goes back to 0.9.2120 (inherited from KindaPlus).
- Thunderstore has no comment system, so there are no user comments to collect. Support goes through GitHub/Discord. Upstream Multivoid Discord: https://discord.gg/bA6tGBvGMN (from the [Multivoid page](https://thunderstore.io/c/voices-of-the-void/p/Pelmentor/Multivoid/), 0.9.208, ~13.5K downloads).
- Known issues stated on the package page: "Every version is a test version"; "Rendered gameplay, real-peer synchronization, and full event coverage are unverified for this package"; "Paid client repair, resource transactions, ancillary MRE effects, and other reported gameplay cases remain open"; all players need the exact same package.
- Bug-relevant changelog items are quoted in section 1. Recurring theme in every entry: "Two-player gameplay verification remains pending." Fixes are verified offline (94/94 groups for 2160), not in real multiplayer.
- Changelog API: `https://thunderstore.io/api/experimental/package/ElvardFIC/WatchersOfTheVoid/0.9.2160/changelog/`.

---

## 4. VotV wiki facts (0.9.0x)

**Main computer, Viewer tab** ([wiki: Computer](https://voicesofthevoid.wiki.gg/wiki/Computer)). Tabs: Upgrades, Database, Email, Store, Advancements, Cameras, Media, Components, **Viewer**. The Viewer "transfers server hash data between floppy discs or zip drives and the computer's internal buffer". It has two panes: the floppy pane (data blocks + remaining reads/writes) and the buffer pane. Blocks move floppy -> buffer -> zip drive (for the daily task) or get erased. The "scrolling hash codes" are these data blocks. Data originates from satellite server rows written to floppies (see Multivoid #47/#53: client rows were corrupted in transit).

**Basement furnace = Cremator** ([wiki: Cremator](https://voicesofthevoid.wiki.gg/wiki/Furnace), "Furnace" redirects there). It is in the Alpha Root Base basement. Use: open the front hatch, insert items, close it, flip the panel switch. The hatch stays locked until the machine is off and the flames are out. Fuel accumulates internally (shown on the panel) and is drained through a side pipe into a Gas Can (wasted if no can is attached or the can is full; min 0.1). Coal gives 31.8; most trash gives 0.1 or less.

**Transformers** ([wiki: Transformer](https://voicesofthevoid.wiki.gg/wiki/Transformer)). The Substation is at X64/Y64. TR1 is at (400,200), TR2 at (-540,232), TR3 at (-400,-475). Stability decays with base power use, and melee, explosions, boars and events can knock them out. Repair: a yellow button opens the maintenance hatch, then 3 minigames (rotary fuses 3x3, binary-weight switches, sine match). A cold start needs all three; otherwise sine only. The big red button sets state to 100. Upgradable: [Transformer Upgrade](https://voicesofthevoid.wiki.gg/wiki/Transformer_Upgrade) (relevant to Multivoid #60).

**Mining farm** ([wiki: Guides/Making Money#Crypto Mining](https://voicesofthevoid.wiki.gg/wiki/Guides/Making_Money)). Store-bought **Frame** + up to 16 **GPU Board** + up to 6 **Cooler**. Boards generate Points, which must be "collected". The rig has a point cap and overheats/explodes if left at the cap or under-cooled. It can run from the Electric Generator instead of draining transformers. A maxed rig makes ~600-900 pts/day. Pre-alpha threads say 0.9 miners heat up faster ([itch](https://itch.io/post/15499526/view-in-topic)). In Multivoid #43 the collect action is done via the rig's display.

**Delivery drone** ([wiki: Delivery Drone](https://voicesofthevoid.wiki.gg/wiki/Delivery_Drone)). It is called by a Store order or the Drone Panel (garage console) and hovers above the Loading Bay outside the garage. It stays until dismissed via the panel. **It cannot be dismissed while its sack is detached.** The sack has unlimited storage; `Use` opens it, `Take` detaches it, and items dropped on a detached sack go in. Sold items pay Points only once the drone reaches HQ (hovers at X720/Y2185, ~7.5 km away). **It only travels while the radio tower works. If the tower is down, orders trigger an email and the drone "leaves automatically once the tower is fixed".** Daily free delivery at 06:00. Since 0.9.0, hooks attach to the sack, not the drone.

**Plushies / figurines** ([Category:Plushies](https://voicesofthevoid.wiki.gg/wiki/Category:Plushies), [Template:Navbox_Items](https://voicesofthevoid.wiki.gg/wiki/Template:Navbox_Items), [Store](https://voicesofthevoid.wiki.gg/wiki/Store)). The wiki says "30 plushies, including 12 Argemia".
- Store (Deco tab), 69 pts each unless noted: Vargskelethor, IGP, Spazmatic Banana, Minerva ("party demon"), Berd, Sam, Monika, Three Cats In A Raincoat (the "cool funy ... (real) (no fake)" names), Svenskfisk (50), Rastabanana (400), Giant Keljoy (800). Also Jolly Wangcore/"jollys award star" and Gooseworx/"cinnamon muncher" in the store plush group.
- Findable: Keljoy plush (starts in your suitcase), Kel doll, Antibreather, Furfur, Erie, Monique, Perkele llama, Lambert, Thiccfus, Maxwell / Argemwell / Gnarpwell / Eriewell / Argemug, Candle Handle, duc, Dream Plush.
- Argemia plushies ([page](https://voicesofthevoid.wiki.gg/wiki/Secrets/Hidden_Items/Argemia_Plushies)): Red/Green/Blue are at fixed world spots. Nuclear Pink/Yellow/Orange need Story/Infinite mode (knock down / dig / climb). Magenta comes from the `lifecrystal` signal. Cyan, Yellow, Black and White come from rituals. Gold: stare at a gold ingot for 1 real hour.
- Seasonal: Halloween (Batty, Candy corn, Keljoy Ghost), Christmas toys, Easter (Moai head statues, bunny doll).
- Figures: only the "Zeta Reticulan" alien figure (Store Deco, 100 pts). The wiki has no "figurine" category. Mods add some (e.g. [acitulen/VisibleAriralsPlusFigurines](https://thunderstore.io/c/voices-of-the-void/p/acitulen/VisibleAriralsPlusFigurines/)).

---

## 5. How VotV is modded / making a custom figurine pak

**Loader stack.**
- [RE-UE4SS](https://github.com/UE4SS-RE/RE-UE4SS) handles Lua/C++ mods, and its BPModLoader loads `LogicMods` paks.
- Thunderstore uses [unreal_shimloader](https://thunderstore.io/c/voices-of-the-void/p/Thunderstore/unreal_shimloader/) (dwmapi.dll proxy, redirected Mods/LogicMods/Config paths; this is how the coop DLL is loaded).
- Blueprint paks go in `VotV/Content/Paks/LogicMods/` (shimloader: the profile's `shimloader/pak` folder).
- UE4SS spawns `Content/Mods/<ModName>/ModActor` (BeginPlay; optional PreBeginPlay/PostBeginPlay).
- C++ setup guide: [modestimpala/UE4SS-Build-Guide](https://github.com/modestimpala/UE4SS-Build-Guide).

**Community wiki.**
- The original `votvmodding.github.io` now 301-redirects to `modding.ariral.space`, which itself redirects to an unrelated domain (bunprint.com). Treat it as dead and do not use it.
- The maintained replacement is [Questwalker/votv-modding-wiki](https://github.com/Questwalker/votv-modding-wiki), hosted at https://questwalker.github.io/votv-modding-wiki/ (updated 2026-10-25). Sections: installing mods, intro, Blueprint modding (setting-up-unreal, creating-a-mod, running-your-mod, using-fusion, using-ghostmappings, guides), Lua, C++, publishing to Thunderstore, useful tools, troubleshooting.
- Discord: EternityDev Games (https://discordapp.com/invite/eternitydevgames, channel `#e-votv-modding`).

**Is there a "VotV modkit" UE project?** Yes, effectively:
1. Create a blank **UE 4.27.2** project (convention: `VotV/VotV.uproject`). Project settings: Generate Chunks on, Share Material Shader Code off, add `/Game` to cooked dirs, editor pref "Allow ChunkID Assignments". Add the DefaultEngine.ini audio fix. VictoryBPLibrary is recommended ([setting-up-unreal](https://questwalker.github.io/votv-modding-wiki/docs/blueprint-modding/setting-up-unreal)).
2. Drop in **ghostmappings**: [modestimpala/VotV_ghostmap](https://github.com/modestimpala/VotV_ghostmap). It is an auto-generated, GUID-matched recreation of every a09n asset (BP hierarchies, interfaces, structs, enums, materials), with no meshes or logic. It targets `a09n` = our 0.9.0n. Old hand-made version: [NynrahGhost/VotV_ghostmapping](https://github.com/NynrahGhost/VotV_ghostmapping).
3. Optional: the VoidMod DevKit ([Gatohost/VoidMod](https://thunderstore.io/c/voices-of-the-void/p/Gatohost/VoidMod/) 2.2.0; VoidMod2 page marked deprecated). Its example mod is [BananaCatPlushie](https://thunderstore.io/c/voices-of-the-void/p/Gatohost/BananaCatPlushie/).
4. Register new items with [NynrahGhost/Fusion](https://thunderstore.io/c/voices-of-the-void/p/NynrahGhost/Fusion/), which merges modded DataTable rows at runtime (add-only). Put `_list_props` / `_list_store` (underscore-prefixed copies using the game struct) in `Content/Mods/<Mod>/_Content/main/datatables/`. The mod name must match the folder name, case included ([modifying-datatables](https://questwalker.github.io/votv-modding-wiki/docs/blueprint-modding/using-fusion/modifying-datatables)).
5. Package: create a PrimaryAssetLabel with a Chunk ID and "Label Assets in My Directory" on. Package for Windows 64-bit, take `pakchunk<N>-WindowsNoEditor.pak` and rename it `<ModName>.pak` ([creating-a-mod](https://questwalker.github.io/votv-modding-wiki/docs/blueprint-modding/creating-a-mod)).

**Closest template for a custom figurine:** [typlosion14/VOTV-Hololive-Plush](https://github.com/typlosion14/VOTV-Hololive-Plush) ([Thunderstore](https://thunderstore.io/c/voices-of-the-void/p/Typlosion/HololivePlush/), a09n). It adds 36 plush/figure props (including a "DeadBeat Figure"). Layout: `Content/Mods/HololivePlush/` with `ModActor`, a `BP_*` per plush on `*Static` meshes, `_Content/main/datatables/_list_props` + `_list_store`, and a `PrimaryAssetLabel`. `DefaultPakFileRules.ini` keeps skeletal meshes out. It needs ghostmap + Fusion. Smaller examples: [Hirokhai/BuyableMannequinPlush](https://thunderstore.io/c/voices-of-the-void/p/Hirokhai/BuyableMannequinPlush/), [Hirokhai/BuyableArgemiaPlushes](https://thunderstore.io/c/voices-of-the-void/p/Hirokhai/BuyableArgemiaPlushes/).

**Asset tools** ([useful-tools](https://questwalker.github.io/votv-modding-wiki/docs/useful-tools)): FModel (view/export cooked assets and JSON; use this instead of unpacking the 8 GB pak), Kismet Analyzer (BP bytecode graphs), UModel, Stove (maps), Blender + PSK/PSA importer. Gale users with Fusion need [JonnyBro/GaleFusionCompat](https://github.com/JonnyBro/GaleFusionCompat).

**Multiplayer implication [?]:** a custom prop class must exist in every peer's pak set. Fusion-merged rows must be identical on all peers, or prop births by class/row will fail on peers missing the mod. Upstream has a "custom content" runtime rule (Multivoid PR [#6](https://github.com/VOTV-MP/Multivoid/pull/6) "Force Custom Content") and loads player models from `LogicMods\multimodel` (fork changelog 0.9.2120). Check how the coop prop lane resolves classes not in the base game before shipping a figurine.

---

## 6. Top leads for the fix team
1. Drone not returning: check that radio-tower repair state and drone-sack attachment are host-authoritative ([#59](https://github.com/VOTV-MP/Multivoid/issues/59), [#42](https://github.com/VOTV-MP/Multivoid/issues/42), wiki rules on tower and sack).
2. Viewer freeze: audit `laptop_ui_sync` operator handover and repaint (commit bf276d14c, changelog 2156) for a stale operator or a forced page repaint. Also check the PR #53 codec fix is in the shipped DLL.
3. Client cremator/rig/transformer use: no sync lane exists in either repo. Apply the PR #38/#34 pattern: refuse the client's local body at the script gate, send an intent, and have the host run the verb and broadcast state.
4. Invisible/stale props: the record-only-on-birth gap (PR #36/#39). Extend `upd()`-style record refresh to more classes (rig frame contents, posters, food cans). Loose physics props are not streamed at all (changelog 2122).
5. Dupes/disappearance: join sweep (fae4bd187/583f96864), floppy eject race (#24), drone sack dup (#42), crate despawn on large sack load (#58).
