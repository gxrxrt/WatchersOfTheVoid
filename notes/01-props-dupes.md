# 01 — Props: physics/collision desync, "invisible until interacted" (#1), duplication / disappearance (#5)

Investigator scope: prop sync end to end (identity, births, deaths, resting-state streaming, mirror
physics, late join, inventory/container edges). Source is READ-ONLY; all paths below are relative to
`WatchersOfTheVoid-main/src/votv-coop/` unless they start with `docs/` or `agent/`.
Tags: **[V]** verified by reading code, **[?]** hypothesis that needs a runtime log or a two-peer repro.

No multiplayer session is present in the local `multivoid.log` (it stops in the server browser), so
nothing below is confirmed from runtime evidence; everything is from code + the maintainers' own docs.

---------------------------------------------------------------------------------------------------

## 1. How the prop system actually works (map)

### 1.1 Identity
- [V] Cross-peer name = the game's own save `Key` (FName) on `Aprop_C`, plus a host element id (eid).
  Host allocates eids from the lower half of the id space, clients from the upper half
  (`docs/coop-entity-expression-map.md` "Identity ranges").
- [V] A client mints NO eid for its save-loaded props; it is bound to the host's eid when the join
  snapshot's PropSpawn resolves it by exact key (`src/coop/props/remote_prop_spawn.cpp:268-271, 384`).
- [V] Receivers write the host key into a fresh mirror BEFORE FinishSpawning via `setKey`
  (`src/coop/props/prop_fresh_spawn.cpp:151-182`).
- [V] Keyless props are only expressible if they are trash (pile/clump), which travel by eid
  (`src/coop/props/prop_snapshot.cpp:306-314`). Any other keyless prop cannot cross the wire.
- [V] Host re-keys duplicate keys at enrolment; clients never re-key
  (`docs/coop-entity-expression-map.md` "Key uniqueness"; `src/coop/props/prop_element_tracker.cpp:277`).

### 1.2 Births (who expresses a new prop)
| Seam | Role | File:line | Notes |
|---|---|---|---|
| `init` POST at the script gate | host (and client for non-Aprop trash) | `src/coop/props/prop_lifecycle.cpp:278-281, 69-267` | only Init calls **with no calling BP frame**; ~0% of real births go through it (comment :71-74) |
| `FinishSpawningActor` Func patch → next-tick drain | host only, connected only | `src/coop/props/host_spawn_watcher.cpp:250-282, 426-537` | catches every BP `SpawnActor`; keyless births retried 8 ticks then **silently dropped** (:506-508) |
| steady re-seed (scan hub, tail pass every 2 s, full every ~20 s) | host broadcasts | `src/coop/props/prop_census.cpp:441-489`, `src/coop/element/object_scan_hub.cpp:26-27` | safety net for missed host births |
| new-held edge `EnsureHeldItemBroadcast` | both roles | `src/coop/props/trash_collect_sync.cpp:222-391` | expresses any **untracked** prop the moment someone grabs it (see §2.2) |
| client drop/place intent | client → host | `src/coop/props/prop_drop_intent.cpp:170-245, 290-520` | only parked keys, whitelisted device births, container extracts, player-authored (menu/toolgun/workbench…) births |
| ambient pinecone/stick/crystal | each peer for its own | `src/coop/props/host_spawn_watcher.cpp:126-220` | keyless, eid identity, peer-symmetric |

- [V] A client's own keyed `Aprop_C` birth is NEVER broadcast by the lifecycle
  (`src/coop/props/prop_lifecycle.cpp:121-150`); it crosses only if the drop-intent drain admits it,
  otherwise it exits as `not-a-place-nor-whitelisted-birth` (`src/coop/props/prop_drop_intent.cpp:402-426`).

### 1.3 Deaths
- [V] `K2_DestroyActor` Func patch on **both** roles (`src/coop/props/prop_lifecycle.cpp:413-444`,
  body `src/coop/props/prop_destroy_seam.cpp:97-249`). Every keyed destroy is broadcast as
  `PropDestroy{key, eid}` — including a **client's** (`prop_destroy_seam.cpp:239`).
- [V] The host applies a client's PropDestroy **unconditionally** — the dispatcher says so:
  "with a bidirectional destroy a client can command the host to destroy any prop by key. Accepted
  for cooperative play" (`src/coop/dispatch/event_dispatch_entity.cpp:270-309`). PropSpawn/PropDestroy
  from a client are also relayed raw to other clients (`src/coop/net/session_lanes.h` relayable list, ~:313-314).
- [V] Host-side BP vanishes that never call K2 (lifespan, truck, cull) are found by the 4 s reaper
  and broadcast by eid — but only if fewer than 64 died in that scan; ≥64 is treated as a world
  purge and **nothing is broadcast** (`src/coop/props/registry_reaper.cpp:63, 183-197, 218-224`).
- [V] Client destroys are suppressed (not broadcast, key not parked) during the client's load
  episode and during **any** reconcile window (join bracket or mid-session re-bracket)
  (`src/coop/props/prop_destroy_seam.cpp:153-176`).

### 1.4 Resting props, streaming, relevance
- [V] There is **no resting-state stream and no periodic resync**. Ownership table:
  "a prop at rest: existence, transform, physics — the host — the birth broadcast"
  (`docs/props.md:352`). After birth/snapshot, position only crosses while a *verb* is active:
  held-prop stream (`src/coop/player/local_streams.cpp:430-470`), host coast/drive stream for hooks,
  brooms, releases, births, explosions, held-prop knocks and the prop a player stands on
  (callers of `prop_drive_host::Coast/Claim`, e.g. `src/coop/props/prop_knock.cpp:65`,
  `src/coop/props/prop_support_sync.cpp:41-45`, `src/coop/player/local_streams.cpp:655`,
  `src/coop/dispatch/event_dispatch_entity.cpp:153-161`).
- [V] No distance/relevance culling for props anywhere (grep of `src/coop/props`, `src/coop/net`
  for relevan*/cull* finds nothing prop-stream related). Births and destroys go to all peers.
- [V] Sleep/rest detection: coast ends 500 ms after moving < 0.5 cm/0.5° (`src/coop/props/prop_drive_host.cpp:45-53`);
  the end edge restores local simulation on receivers, so copies re-settle separately (doc'd 0–12 cm,
  `docs/props.md:593`). Host births coast at most 32 at a time (`host_spawn_watcher.cpp:402-414`).

### 1.5 How a mirror is simulated on the receiver
- [V] Mirrors are **fully simulated locally** (single-player parity): `SetSimulatePhysics(sim)` with
  `sim = !(static|frozen|sleep)` (`src/coop/props/prop_fresh_spawn.cpp:276-290`,
  `src/coop/props/prop_wire_parity.cpp:99-109`). Kinematic only while parked under a host drive or a
  peer's hold. Collision stays on (except the join-window hide, §2.4).
- [V] Remote player puppets are **QueryOnly** — capsule and meshes cannot push physics bodies on the
  observing peer (`src/ue_wrap/actors/puppet_spawn.cpp:144-157`).
- [V] The only body-contact lane is `prop_knock`: it coasts a prop only if it was hit by a prop that
  is held/driven (`src/coop/props/prop_knock.cpp:27-32, 50`). A player's capsule (local or puppet)
  kicking a prop is caught by nothing; `prop_support_sync` covers only the prop a player stands ON.

### 1.6 Late join
- [V] Client loads the host's live-captured save, then the host sends one PropSpawn per tracked prop
  (100 per tick, `src/coop/props/prop_snapshot.cpp:141, 451-554`), client binds by exact key /
  fuzzy 30 cm / fresh spawn (`remote_prop_spawn.cpp:243-546`), corrections via PropSnapPos, then a
  quiescence-gated sweep destroys unclaimed client props with a per-class floor and a 50% valve
  (`src/coop/props/join_membership_sweep.cpp:131-505`).
- [V] Fresh host mirrors created between SnapshotBegin and load-tail quiescence are **hidden with
  collision off** (`src/coop/element/mirror_defer.cpp:71-78`, armed `src/coop/dispatch/event_feed.cpp:334`),
  revealed at SnapshotComplete for non-matchPos rows (:80-94) and the rest at quiescence
  (`join_membership_sweep.cpp:592`), quiescence ceiling 120 s (`src/coop/session/world_load_episode.cpp:85`).

---------------------------------------------------------------------------------------------------

## 2. Bug #1 — "props invisible to the other peer until someone interacts"

### 2.1 The smoking gun: the grab edge is the late catch-up channel  [V]
`EnsureHeldItemBroadcast` (`src/coop/props/trash_collect_sync.cpp:283-300`, comment verbatim in
spirit): *"Has a key" is not "the peer has it" … an untracked one is expressed here, key and all, the
moment a player first touches it.* For an untracked Aprop it even force-mints a key
(`:320`) and sends `PropSpawn` (`:383-386`). So the code base already knows props exist on one peer
and not the other, and the **first grab is what makes them appear**. The maintainers hit one case
explicitly: *"The workbench finishes crafted output … Without this provenance the client's output is
discarded by the drop-intent whitelist until picked up"* (`src/coop/props/prop_spawn_authoring.cpp:158-160`).
Every invisible-until-touched report is therefore a birth that some seam failed to express. The
failing seams, in order of likelihood:

### 2.2 Root cause A (high): client-side births outside the whitelist never reach the host  [V]
- Client Aprop births are never broadcast (`prop_lifecycle.cpp:132-150`); the drop-intent drain
  admits only: a parked key (re-place after a pickup), reels/modules/drives/discs/server-upgrades/
  sleeping bags, container extracts, notebook sheets, and births whose **caller frame** is one of the
  hand-listed player graphs (spawn menu, toolgun, workbench, bbq lid, MRE, foodBox/banana/coffee
  husks, cookItem on the hand item, sleeping bag/bunk graphs) (`prop_drop_intent.cpp:388-426`,
  `prop_spawn_authoring.cpp:143-222`). Everything else is dropped as
  `not-a-place-nor-whitelisted-birth`.
- Not covered, from the code's own lists: `lib_C::replaceProp` births (breaking/impact gibs —
  explicitly classed as "the world's" at `prop_spawn_authoring.cpp:45, 136`), any slicing/cutting,
  any other container/package that spawns contents, any BP factory not hand-listed. "Client breaking"
  is still an open acceptance item (`agent/TODO.md:665`).
- Result on host/other clients: the item does not exist → invisible. When the client grabs it, the
  held edge expresses it → it pops into existence for everyone. Exactly the reported symptom.

### 2.3 Root cause B (high): destroys cross, births don't — client-only conversions  [V]+[?]
Every client runs the full Blueprint brain of every prop. When a client-side graph converts A→B
(break, open, transform, burn, etc.):
1. A's `K2_DestroyActor` on the client is broadcast and the host obeys blindly
   (`prop_destroy_seam.cpp:239`, `event_dispatch_entity.cpp:270-309`) → A vanishes on the host;
2. B is a client birth → dropped at the drop-intent door (§2.2);
3. The host's own copy of A is gone before the host's graph could produce its own B.
Net: A disappears everywhere, B exists only on the client (invisible until grabbed). This is also a
major **disappearance** path for #5. [?] Which concrete VotV graphs do this on clients needs a log
grep: `[PROP-DROP] destroy-received role=host sender=N cls=...` (`remote_prop_destroy.cpp:299-301`)
correlated with client `[PROP-DROP] ... not-a-place-nor-whitelisted-birth` exits
(enable `VOTVCOOP_PROP_BIRTH_KEY_PROBE=1`).

### 2.4 Root cause C (medium): fresh host mirrors hidden + collision-off during the join window  [V]
`mirror_defer::OnMirrorSpawned` hides every fresh host mirror (and disables collision) while armed
(`mirror_defer.cpp:71-78`). Armed at SnapshotBegin, it stays armed for live (non-snapshot) spawns
until load-tail quiescence — up to the 120 s ceiling. A prop the host spawns/drops in that window is
invisible and non-collidable on the joiner. In addition `TickClientReconcile` has several
`if (!pass.Current()) return;` exits between `RunDivergenceSweep_` and
`RevealAllSurvivorsAtQuiescence` (`join_membership_sweep.cpp:567-592`); if the claim epoch changes
there (a re-bracket, world-ready reset) the hidden set is only revealed at the next `Arm()`/`Reset()`.
Mid-session re-brackets also re-arm it (`registry_reaper.cpp:202-210` retriggers snapshots after a
re-seed → SnapshotBegin → `mirror_defer::Arm()`).

### 2.5 Root cause D (medium): host keyless births are never expressed  [V]
- Host finish-spawn drain gives an unkeyed birth 8 ticks, then drops it with no log
  (`host_spawn_watcher.cpp:506-508`; only the floor-bed branch at :490-491 logs).
- The snapshot/incremental builder refuses keyless non-trash (`prop_snapshot.cpp:306-314`).
- The steady re-seed inserts the actor into the known set **before** checking it has a key
  (`prop_census.cpp:447-461` vs `:463-473`), so a prop first seen keyless is permanently "known" and
  never re-evaluated after it gains a key. Same for `MarkKnownKeyedProp` at
  `prop_lifecycle.cpp:93`, which runs before the connected/keyless gates (:97, :187).
- Only the grab edge can save it (it force-mints a key, `trash_collect_sync.cpp:320`). The sleeping
  bag wrap is a measured instance of a class born with Key=None (`prop_lifecycle.cpp:300-325`); there
  may be others.

### 2.6 Root cause E (lower): births lost to caps / windows  [V]
- Host pending-finished cap 128 (`host_spawn_watcher.cpp:242, 266-271`) and client pending-place cap
  32 with only 2 drained per tick (`prop_drop_intent.cpp:136, 225-229, 290`) — a burst (spawn menu
  pile, container dump) drops the overflow. Host overflow is backstopped by the re-seed; client
  overflow is not.
- Client place detection is off during the load episode / load-kind reconcile window unless
  player-authored (`prop_drop_intent.cpp:199-211`).

### 2.7 Physics / collision desync (the other half of #1)

**Root cause F (high): nothing reconciles resting props after the first contact**  [V]
- Mirrors simulate locally (§1.5) and there is no periodic or rest-state correction (§1.4).
- Player bodies do not push props on the remote peer (puppets QueryOnly, `puppet_spawn.cpp:144-157`),
  and no lane reports a capsule push (`prop_knock.cpp:27-32,50` needs a held/driven source;
  `prop_support_sync.cpp:51-80` only the prop under the feet). So when either player walks into,
  kicks, or brushes a prop, only that peer's copy moves, permanently. NPC/creature pushes, settling
  stacks, and anything the host simulates without a "verb" diverge the same way.
- Coast end edges hand simulation back to each copy, which then settles independently
  (`docs/props.md:593`), and bursts past 32 coasting births fall locally (`host_spawn_watcher.cpp:402-414`).
- Visible effect: props in different places, players colliding with things the other doesn't see,
  "phantom" collisions, items stacked on the client lying on the floor on the host. A grab fixes it
  (the hold stream converges the copy), which again matches "until someone interacts".

**Root cause G (medium): a static/kinematic copy refuses the drive**  [V]
`docs/props.md:147-148, 587`: a copy static on one peer is not driven while another peer holds it;
toolgun freeze/static (`setPropProps`) is not synced at all. So a prop frozen on one machine stays put
there while it moves for the holder.

---------------------------------------------------------------------------------------------------

## 3. Bug #5 — duplication and disappearance: every path

### 3.1 Matrix (spawned twice / destroyed without counterpart)
| # | Path | Result | Evidence | Conf. |
|---|---|---|---|---|
| D1 | Client-side BP conversion/break (A→B): A's destroy crosses, B doesn't | A gone everywhere, B client-only (looks like a dupe on client, loss on host) | §2.3 | high [V] mechanism, [?] which graphs |
| D2 | Client destroy of a keyed prop from its own world brain (any BP `DestroyActor`: kill volume, collector, burn, timer) | host loses its authoritative copy (and other clients via relay) | `prop_destroy_seam.cpp:239`, `event_dispatch_entity.cpp:270-309` | high [V] mechanism, [?] frequency |
| D3 | Pickup / eat / destroy during load episode or ANY reconcile window (join bracket; mid-session re-bracket after a ≥64-prop purge, host save load, travel) | destroy not sent and key not parked → host keeps original (DUPE); the later place is dropped as not-a-place → client-only ghost, or `Refused` because the key still exists | `prop_destroy_seam.cpp:153-176`; `prop_drop_intent.cpp:683-687, 572-588`; `registry_reaper.cpp:202-224` | high [V] |
| D4 | Host mass vanish (≥64 non-K2 deaths in one 4 s reaper scan: truck/cleanup/lifespans/explosion) | no PropDestroy broadcast → client keeps them (ghost dupes) until a re-bracket sweep; re-bracket re-opens D3 and §2.4 windows | `registry_reaper.cpp:63, 183-197, 218-224` | medium [V] |
| D5 | Per-peer natural spawners not on `spawn_authority`'s refusal list | both peers spawn; only the 30 cm same-class/row fuzzy adopt dedups; random placements >30 cm → client sees two | `src/coop/world/spawn_authority.cpp:68-129`, `remote_prop_spawn.cpp:387-418`, `docs/props.md:70-78` | medium [?] (needs list of unrefused spawners) |
| D6 | Fuzzy adopt rekeys the wrong local prop (same class+row within 30 cm, not wire-bound: client-born, refused placements, failed binds) | that prop's original key disappears on the client; later host ops on it miss → dupe/loss pair | `remote_prop_spawn.cpp:412-532` (guard only for wire-bound, :444-455) | low-medium [V] |
| D7 | Unbound keyed prop picked up / eaten on client after world-ready: `ClientGate` runs it locally (no host arbitration) | if host also takes it simultaneously, both have it | `src/coop/props/prop_act_request.cpp:354-368` | low-medium [V] |
| D8 | Simultaneous physics grab: no pre-grab host grant; arbitration only after poses arrive | rubber-banding, wrong final pose, occasionally the loser's release throws it | `docs/props.md:470-474, 584` | low (not a dupe) [V] |
| D9 | Refused placement stays as a client-local actor ("recover locally by picking it back up") | client sees an item the host doesn't (invisible), picking it back up broadcasts a destroy by key that kills the host's same-key original | `prop_drop_intent.cpp:579-583` | medium [V] |
| D10 | Profile persistence: host stores client inventories in memory, cuts to disk only when the host saves; profile streamed "as it changes" | item picked up just before a client drop/crash → missing on rejoin; item dropped just before → duplicated on rejoin; host quits without saving → world & profiles roll back together (OK) | `include/coop/items/player_inventory_sync.h:1-20` | low-medium [?] |
| D11 | Join sweep false doom: unclaimed client props destroyed if host under-expressed (keyless, failed rows) but class census incomplete detection misses | client-only loss | `join_membership_sweep.cpp:225-267` (floor), :275-287 (valve) | low [V] |
| D12 | Host re-key of a duplicate key after the joiner's save capture | client has old key → host row fresh-spawns a mirror beside it until the sweep | `prop_element_tracker.cpp:277` | low [V] |
| D13 | Sublevel streaming/cave unload treated as host-side vanish | destroys props on a peer still in that area | comment `registry_reaper.cpp:177-182` | low [?] |

### 3.2 Covered paths (for completeness — these look correct)  [V]
- Pocket pickup of a host-bound prop by a client → host-granted act (`prop_act_request.cpp:405-428`);
  food/MRE/pills/soap final use likewise (`:251-260`).
- Place after pickup → parked key → host spawns, echo resolves to the client's own actor
  (`prop_drop_intent.cpp:600-607`, `remote_prop_spawn.cpp:268-269`).
- Host refuses a same-key placement (`prop_drop_intent.cpp:681-687`) — prevents a dupe but feeds D9.
- Container extract → admitted intent (`prop_drop_intent.cpp:231, 401-402`); contents slices host-arbitrated (`docs/props.md:208-221`).
- Hand-view actor destroys are not broadcast (`prop_destroy_seam.cpp:194-202`).
- R+LMB release-before-echo guarded (`trash_collect_sync.cpp:225-228`).

---------------------------------------------------------------------------------------------------

## 4. Proposed fixes (ranked by confidence × impact, blast radius noted)

### F1 — Validate client PropDestroy on the host (fixes D1, D2, half of §2.3). Confidence high, radius medium.
Where: `src/coop/dispatch/event_dispatch_entity.cpp:270-309` → `remote_prop::OnDestroy`.
Change: when `senderPeerSlot != 0` and the resolved actor is host-authoritative (non-mirror host
element), do NOT destroy unless one of: (a) it is the target of a granted act (`prop_act_host`
reservation), (b) it is currently held by that sender's stream (pickup-from-hand), (c) the sender
is within reach and the class is on a small "player-verb destroy" list, (d) the actor is a mirror of
that client's own birth. Otherwise log `[PROP-DESTROY-REFUSED]` and re-express the prop to that
client with `prop_snapshot::ExpressIncrementalSpawn(actor)` so its copy comes back. Same for relaying:
stop relaying raw client PropDestroy (`session_lanes.h` relay list); let the host's own destroy
broadcast propagate instead. Producer-side complement: in `prop_destroy_seam.cpp:239` a client
should only send for destroys inside a player verb (pickup/eat/use bracket) — everything else is the
client's local world churn, which the host's own brain or reaper authors.

### F2 — Express client gameplay births instead of dropping them (fixes §2.2, D1). Confidence high, radius medium.
Where: `src/coop/props/prop_drop_intent.cpp:402-426` (`not-a-place-nor-whitelisted-birth`).
Change: replace the whitelist with a provenance test: admit a client birth as a drop intent when the
calling frame's owner is a prop/BP the local player is interacting with (the destroy of its source
happened in the same tick and was a local-player verb, or the source prop is within reach of the
local player), and exclude loader frames (`mainGamemode_C` load functions), spawners, and
refused-spawner products. Host side `FreshPlacementAllowed` (`:623-654`) must accept the same class
set. Lower-risk interim: add `lib_C::replaceProp` / `comp_physicsImpact_C` callers to
`BirthIsPlayerAuthored` (`prop_spawn_authoring.cpp:143-222`) only when the source actor was held
by, or within ~3 m of, the local player. Pair with F1 so A's destroy and B's birth are both judged.

### F3 — Host "rest reconciler": stream any awake host prop and accept client nudges (fixes §2.7 F). Confidence high, radius medium (bandwidth).
- Host: a budgeted pass (e.g. 64 props per tick over the registry's Prop elements) that calls
  `prop_drive_host::Coast(actor, "awake")` for any tracked, non-held, non-driven prop whose root body
  is not at rest (`engine_attach.h IsActorRootBodyAtRest`, already used by `prop_wire_parity.cpp:64`).
  The existing coast stream + `PropDriveEnd` then converges every client automatically
  (`prop_drive_host.cpp`, `prop_drive_stream.cpp`). Cap and prioritise by distance to any player.
- Client: extend `prop_support_sync.cpp:51-80` into a "contact" lease — when a client-local copy that
  is a host mirror wakes while not held/driven and within ~2 m of the local player, send the existing
  `PropSupport` (or a new `PropNudge` with pose+velocity); host authorises with
  `IntentTarget::ForClientIntent` (as `:75-81`), applies the pose/velocity to its copy and Coasts it.
- Optional cheap safety net: every ~5 s the host sends `PropSnapPos`-style corrections for props near
  players whose host pose moved >5 cm since last sent (PropSnapPos today is join-only and host-eid
  only, `event_dispatch_entity.cpp:~370-420`).

### F4 — Stop suppressing genuine client destroys in mid-session reconcile windows (fixes D3). Confidence high, radius low.
Where: `src/coop/props/prop_destroy_seam.cpp:153-176`.
Change: suppress only while `InEpisode()` (the client's own load churn). In a reconcile window that is
`!ReconcileWindowIsLoadKind()` (mid-session bracket), send normally. When suppressing, still call
`prop_drop_intent::NoteClientKeyedDestroy(key)` if the destroy is a pickup (hand/pocket verb) and
queue the PropDestroy for send at window close instead of dropping it.

### F5 — Bound the join-window hide to snapshot rows (fixes §2.4). Confidence high, radius low.
Where: `src/coop/element/mirror_defer.cpp:71-78`, `src/coop/props/prop_fresh_spawn.cpp:313`,
`src/coop/dispatch/event_feed.cpp:410`.
Change: after `RevealConfirmedAtLift` (SnapshotComplete) set a flag so `OnMirrorSpawned` no longer
hides new live spawns; only rows with `hasMatchPos` stay held. Move
`RevealAllSurvivorsAtQuiescence()` so it runs before any `pass.Current()` early return in
`join_membership_sweep.cpp:567-592` (or call it from the epoch-change paths
`OnClientWorldReadyResetSweep`/`BeginClaimTracking`).

### F6 — Host keyless births: mint and express; fix the re-seed known-set order (fixes §2.5). Confidence medium-high, radius low.
- `src/coop/props/host_spawn_watcher.cpp:506-508`: when tries exhaust and the actor is a live,
  untracked, non-hand, non-mirror `Aprop_C`, mint a persistent key with
  `prop_synth_key::MintFreshKeyForDuplicate` (exactly as `prop_lifecycle.cpp:309-325` does for the
  sleeping bag) and call `ExpressSpawnedProp`; log the class once.
- `src/coop/props/prop_census.cpp:447-461`: insert into `g_knownKeyedProps` only when `expressible`
  (compute the key first), so a later-keyed actor is picked up by the next full pass.
- `src/coop/props/prop_lifecycle.cpp:93`: move `MarkKnownKeyedProp` after the key/None guard (:187).

### F7 — Mass-vanish broadcast (fixes D4). Confidence medium, radius low.
`src/coop/props/registry_reaper.cpp:183-197`: when `reaped >= kReseedPurge` but the current world is
still the stamped gameplay world (`IsRegistrySeededForCurrentWorld()` true, no travel), it is not a
world purge — broadcast the eids anyway (paced), and do not retrigger snapshots.

### F8 — Refused placement must not leave a local ghost (fixes D9). Confidence medium, radius low.
`src/coop/props/prop_drop_intent.cpp:579-583`: on `Refused`, if a same-key host actor exists, destroy
the local actor (echo-suppressed) and ask the host to re-express that key; otherwise put the item
back into the inventory instead of leaving it in the world.

### F9 — Narrow fuzzy adoption (fixes D6). Confidence medium, radius low.
`src/coop/props/remote_prop_spawn.cpp:412-455`: skip candidates that carry a valid key that the key
index maps to a *different* live identity known to the host (i.e. any key that was ever bound), and
skip actors in `prop_drop_intent` awaiting/refused sets; shrink radius to ~10 cm for non-spawner classes.

### F10 — Inventory profile flush on change edges (fixes D10). Confidence low-medium.
Host should persist a client's profile on pickup/drop edges (or at disconnect with the last
consistent world state) rather than only at host world save — see `coop/player/player_profile_store`.

---------------------------------------------------------------------------------------------------

## 5. How to confirm quickly (logs to grep in `multivoid.log`, both peers)
- Client births dropped: enable `VOTVCOOP_PROP_BIRTH_KEY_PROBE=1`; grep
  `not-a-place-nor-whitelisted-birth` and `[ROCK-DROP] CLIENT Aprop spawn not expressed`.
- Late catch-up: grep `trash_collect: BROADCAST held untracked item` — each line is a prop that was
  invisible to the other peer until this grab. Class names = the missing birth paths.
- Client destroys the host obeyed: on host grep `[PROP-DROP] destroy-received role=host sender=1`
  and compare with client `grab_hook[destroy-seam]: CLIENT broadcasting DESTROY`.
- Suppressed destroys (dupes): client `CLIENT suppressed KEYED DESTROY` / `CLIENT suppressed ... DESTROY`.
- Mass vanish: host `mass-purge detected (reaped N >= 64)` during gameplay.
- Hidden mirrors: client `mirror_defer: ARMED` without a later `quiescence-reveal`.
- Keyless host births: add a log at `host_spawn_watcher.cpp:507` (currently silent).
- Physics desync: no log exists — reproduce by kicking a crate as client and comparing positions;
  nothing will be sent (`[PROP-DRIVE]` absent).

## 6. Open questions [?]
- Exact list of client-side BP graphs that destroy/convert props without a player verb (D1/D2) and of
  per-peer spawners not refused (D5) — needs a Blueprint census or a field log with the probes above.
- Whether VotV streams sublevels (caves) — affects the reaper assumption (D13).
- Whether `Hold Object` (`prop_act_request.cpp:254`) is the physics-handle grab or the hotbar hold;
  if the physics grab is unarbitrated, simultaneous grabs are D8 only (not a dupe).
