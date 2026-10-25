# 06 — Bug #6 (the delivery drone sometimes never flies back) + a "small Claude" figurine

Paths are relative to `WatchersOfTheVoid-main/src/votv-coop/` unless they start with `docs/`, `agent/` or `assets/`.
`[V]` = verified in code or docs. `[?]` = hypothesis. The drone's Blueprint disassembly is not in this zip
(`agent/tests/blueprint_text_drone_2135_test.py:4-5` reads `_research/`, which is absent), so anything about the
drone's *internal* BP logic comes from the mod's comments and drills, not from bytecode.

---------------------------------------------------------------------------------------------------
## Part A — The drone

### A1. What the mod knows about the drone's state machine

Fields the mod reads or writes on `drone_C` (the world singleton, `src/ue_wrap/devices/drone.cpp:211-214`):

| Field | Meaning (per the mod) | Where |
|---|---|---|
| `Active` | flying a delivery (true) or dormant (false) | `src/ue_wrap/devices/drone.cpp:33,216-219` |
| `canTakeOff` | "arrived": the drone is within 25 cm of its drop point. This is the interaction gate ("drone is in motion" when false) | `include/ue_wrap/devices/drone.h:66-70`, `src/ue_wrap/devices/drone.cpp:64-67,373-383` |
| `hasSack` | cargo sack aboard. The open-inventory and drop-sack options need it | `include/ue_wrap/devices/drone.h:68-73`, `src/ue_wrap/devices/drone.cpp:68` |
| `container` | the far-away `prop_inventoryContainer_drone_C` that holds the cargo (GObjStack) | `src/ue_wrap/devices/drone.cpp:69,385-398` |
| `leaveAfter5min` | departure mode: true = leave after 5 min, false = wait until sent | `src/ue_wrap/devices/drone.cpp:158-188` |
| `hasOrder`, `flyingType`, `pickedUp` | only the dev probe reads these (meaning not documented) | `src/coop/dev/drone_probe.cpp:301-310` |
| `sellLocation` | the actor that `sendShop`/`beginFly` fly toward or away from | `src/ue_wrap/world/order_economy.cpp:71-77,199-201` |

BP verbs the mod names (fingerprints): `beginFly, checkOrders, compileOrder, dropSack, player_use, sendShop,
soundAlarm, triggerFly, ReceiveTick` (`include/ue_wrap/facts/fingerprints_0.9.0n.inc:135-144`). Drone verbs run
inside the BP (`EX_LocalVirtualFunction`), so ProcessEvent hooks cannot see them, but the mod can *call* them through
ProcessEvent (`src/coop/dev/drone_probe.cpp:135-138`).

The lifecycle as the mod understands it:

1. **Order.** `ui_laptop.makeAnOrder` → `addOrderCart` (saveSlot.orders) → `drone.sendShop` (`src/coop/items/order_sync.cpp:188-201`).
   If the drone is busy, the order waits in the queue, and the drone takes the next one when it arrives
   (`include/ue_wrap/world/order_economy.h:77-82`). The radio tower must be intact or `sendShop` aborts
   (`src/coop/dev/drone_probe.cpp:181-201`).
2. **Inbound flight.** `Active=1`. A per-tick force integrator on the root sphere `coll` follows a spline
   (`include/ue_wrap/devices/drone.h:5-11`).
3. **Parked.** Once it is within 25 cm of the drop point, `canTakeOff` goes 0→1, the alarm sounds and the light comes on
   (`include/ue_wrap/devices/drone.h:66`). Players open the container or **drop the sack** (`dropSack`, which spawns a
   `prop_dronesack_C` prop).
4. **Departure.** Two ways out:
   - (a) the console keyboard calls `drone.triggerFly(console)`
     (`src/ue_wrap/devices/drone_console.cpp:85-99`). The lid gate is in the console's ubergraph, not in
     triggerFly (`pop_flow_if_not(opened)`, `agent/tests/blueprint_text_drone_2135_test.py:14`).
   - (b) the five-minute timer path, which is gated by `pop_flow_if_not(leaveAfter5min)`
     (`agent/tests/blueprint_text_drone_2135_test.py:16`).
5. **triggerFly's sack rule** (from the drill's observed behaviour, `src/coop/dev/drone_call_drill.cpp:55,369-371`):
   - with the sack aboard, it flies;
   - with no sack aboard and none in the world, a press *puts a sack aboard and is refused*, and the next press flies;
   - with no sack aboard while **a sack lies anywhere in the world, the press is refused every time**.
6. **Return flight** to `sellLocation`. Then `Active=0`, and the drone takes the next queued order.

### A2. Who owns what, and how the client mirror is driven

- **The host owns the drone `[V]`.** It streams pose, `active`, the FX/gate bits and rotor pitch as `DroneState`:
  - at about 20 Hz while Active or while the dust is on, or while the drone moves;
  - immediately when the bits change;
  - otherwise as a heartbeat every 2 s (`src/coop/interactables/drone_sync.cpp:281-311`, heartbeat test at `:300-301`).
- Only the host can send DroneState. A client's copy is refused at the host's receive edge (`src/coop/net/client_authorship.h:210`).
- **The client is always a mirror `[V]`.**
  - `SuppressTick` (`SetActorTickEnabled(false)`) runs once, latched by a bool (`src/coop/interactables/drone_sync.cpp:52,182,316`).
  - The pose goes through a 75 ms LerpWindow (`:139-163`).
  - Every packet writes `canTakeOff` and `hasSack` onto the mirror (`:197`, `src/ue_wrap/devices/drone.cpp:373-383`) and re-points `container` (`:199-200`).
  - Dust, the cue and the light are replayed (`:206-229`).
  - The client's own `sendShop` is refused at the script gate "whoever calls it" (`src/coop/items/order_sync.cpp:233-235`).
  - The client keeps the host's last word on Active in `HostActive()`, but never writes it into the mirror's `Active` field (`include/coop/interactables/drone_sync.h:47-51`).
- **The console is a client intent `[V]`.**
  - The client gates `droneConsole_C.actionOptionIndex` (action 4) at the script gate. A keyboard or leave-timer face press is cancelled locally and queued as a `DroneFlyIntent` (3 s expiry, max 4) (`src/coop/interactables/drone_call_intent.cpp:184-210,234-243`).
  - The host checks reach to a console within 400 uu (`:115-143`), then its own copy of the lid (`:162-167`), then calls `TriggerFly` (`:168-180`).
  - Every refusal is **log-only**. The client gets no feedback (`:138-143,162-175`).
- **The sack and the cargo ride the ordinary prop lanes** (`include/coop/interactables/drone_sync.h:15`). The contents ride the container lane (`docs/devices.md:105-128`).
- **The host's drone tick is never touched by the mod `[V]`.**
  - `SuppressTick` is reached only from `ApplyState` (fed by DroneState, which never reaches the host) and from the client branch of `Tick` (`src/coop/interactables/drone_sync.cpp:182,312-316`).
  - The NPC and world-actor "parking" systems do not include `drone_C`.
  - So "host's drone parked too" is **ruled out**.

### A3. What can stall it — ranked

**S1 — The sack-return overlap is not owned by any lane `[V]` gap, `[?]` exact outcome. Most likely cause.**
`include/coop/interactables/mirror_slot_entry.h:5-13` says it outright: "Two such takes are not refused yet, since no
lane carries what they change ... and the sack the drone puts on." So putting the sack back on the drone is a native
overlap that runs **independently on each peer**. Consider the case where the host dropped the sack (host
`hasSack=0`, birth broadcast) and a client carries it back:
  - **Client world:** the mirror drone takes the sack. Locally `hasSack=1`, and the sack actor is destroyed. That
    destroy crosses to the host (`docs/props.md:42`), so **the host loses its sack copy while its own `hasSack` stays 0**.
    Within ≤2 s the host's heartbeat writes `hasSack=0` back onto the client (`src/coop/interactables/drone_sync.cpp:197,300-301`).
  - **Host state afterwards:** `hasSack=0` and no sack in the world. Per A1.5 the next console press only puts a sack
    aboard and is refused; a second press is needed. If the 5-minute departure also needs the sack `[?]`, the drone
    waits forever. The client sees its first press do "nothing", and gets no message (A2).
  - **Variant S1b (permanent stall):** the host's copy of the sack survives. This happens if the destroy was raced or
    refused, if the host's own overlap took the mirror first and then the client's copy also fired, or if the copy is
    one of the bug-#1 "invisible" ghosts. Then a sack "lies in the world" on the host, and per A1.5 **every**
    `triggerFly` is refused until someone puts *that* sack back. It may be invisible to clients, and the host may not
    know it is there.
  - **Variant S1c:** the host's own overlap with the mirror fires while the client still holds it. The host takes the
    sack and relays the destroy, and the client's carry loses it mid-hand. That is a dupe or loss, not a stall.

**S2 — A client's "drop sack" runs only on its own mirror `[V]`.** `drone_C.actionOptionIndex` and `dropSack` have no
gate. The only `actionOptionIndex` drone watch is on `droneConsole_C` (`src/coop/interactables/drone_call_intent.cpp:41-42`).
The toggle table has no drone row (`src/coop/interactables/toggle_verbs.cpp:52-80`). The mod *deliberately* writes
`hasSack` onto the mirror so that those options appear on the client (`src/ue_wrap/devices/drone.cpp:373-383`).
- A client drop spawns a sack prop on the client only. "Any other keyed prop a client creates is dropped at that
  door and never reaches the host" (`docs/props.md:110-111`).
- The host's drone keeps `hasSack=1`. The heartbeat restores the client's `hasSack=1` within 2 s, so the client can
  **drop again and again: sack duplication** (ties to bug #5).
- Putting such a ghost sack back changes nothing on the host. This feeds S1b-like confusion on later deliveries (which
  sack is "the" sack?).

**S3 — The host refuses a client's console press because of desynced lid or reach `[V]` mechanism, `[?]` frequency.**
- The host reads **its own** copy of the lid (`src/coop/interactables/drone_call_intent.cpp:162-167`; the lid rides the
  door_box lane, `src/coop/interactables/toggle_verbs.cpp:62-63`).
- Reach is measured from the host's puppet of the client (`:115`).
- If the lid is desynced (2157 still lists door/box join failures as open, `agent/SYNC_2157_FIXES.md:8-10`,
  `agent/TODO.md:56-59`) or the puppet is stale, the press is DENIED silently. With `leaveAfter5min=0` ("wait
  indefinitely") the console is the **only** way out, so from the client's side the drone never leaves.
- Presses older than 3 s are dropped silently (`:238,249`).

**S4 — The drone is stuck in flight short of its pad: `Active=1`, `canTakeOff` never goes 1 `[?]`.**
- Arrival needs the drone within 25 cm of the drop (`include/ue_wrap/devices/drone.h:66`).
- On the host, any body sitting on the pad blocks the `coll` sphere: a client's puppet capsule, or a prop that sits
  there **only on the host** (bug #1 position desync or invisible prop). The client cannot see it, so it cannot be moved.
- The same applies to the return path near the base. Nothing watches for this.

**S5 — Client-side BP continuations on the suppressed mirror `[?]`.**
- `SetActorTickEnabled(false)` stops `ReceiveTick` but **not** latent actions or timers. UE4 processes a non-ticking
  actor's latent actions in the world's end-of-frame pass.
- A Delay or timer that the client's own drone BP armed at load (the save was taken mid-delivery) could still call
  `beginFly`, `checkOrders` or `dropSack` on the client. `sendShop` is refused there, but the others are not.
- Effects are mostly cosmetic, since the pose and gate fields get overwritten, but a client-side `dropSack` would mint
  a ghost sack (S2).

**S6 — A recreated mirror loses its tick suppression `[V]` small risk.**
- `g_m.suppressed` is a bool that resets only on disconnect (`src/coop/interactables/drone_sync.cpp:52,182,316,327-331`).
- If the client's `drone_C` actor is ever recreated inside one session, the new actor's flight tick runs and fights
  the stream.

**Ruled out / fine `[V]`:**
- Client orders are priced and committed on the host, then queued natively (`src/coop/items/order_sync.cpp:259-330`).
  A world event's automatic order on a client is not sent (`:211-216`).
- The client's queue follows the host's (`docs/devices.md:398-406`).
- The client never flies the drone.
- Reliable stream plus 2 s heartbeat: a parked mirror cannot miss the final state for long.

### A4. Proposed fixes (ranked by confidence and value / blast radius)

**F1. Host watchdog (new `coop/interactables/drone_watchdog.cpp`, ticked from the host branch next to
`drone_sync::Tick`, `src/coop/session/subsystems.cpp:898`). High value, contained.**

Read at 4 Hz on the host:
- `Active`, `canTakeOff`, `hasSack`, `leaveAfter5min`, pose;
- `sackInWorld = world_singleton::Find(L"prop_dronesack_C")`, the same probe the drill uses (`src/coop/dev/drone_call_drill.cpp:127`);
- whether any peer currently carries that sack (prop claim/carry registry).

Keep `stateSince` per tuple and `lastMovedMs`, meaning the last time the pose moved more than 50 cm.

- **Parked overdue:** `Active && canTakeOff && leaveAfter5min` for more than 300 s + 90 s grace (ini
  `drone_wd_parked_s=390`).
  1. **Reconcile the sack.** If `!hasSack` and there is an uncarried sack in the world, destroy it through the normal
     host destroy (peers follow through PropDestroy), then write `hasSack=1` (wrapper already has the offset,
     `src/ue_wrap/devices/drone.cpp:68,381-382`). Better, once it is reverse-engineered: call the drone's own
     sack-entry verb. If there is no sack anywhere, skip this step: triggerFly installs one.
  2. Call `drone_console::TriggerFly(firstLiveConsole)` (`src/ue_wrap/devices/drone_console.cpp:85-99`). This bypasses
     the lid, which is right for a watchdog. If it is still parked 3 s later, call it once more (the "first press
     installs a sack" rule).
  3. Verify within 10 s that `canTakeOff` dropped or the pose moved more than 2 m. Log `[DRONE-WD]` with the full tuple.
- **Parked with "wait indefinitely":** never force a departure, because the player chose it. Still run the sack
  reconcile when `!hasSack` and the sack has been uncarried and within 3 m of the drone for more than 5 s. That repairs
  S1 at once.
- **Stuck in flight:** `Active && !canTakeOff` and no 50 cm movement for 45 s (`drone_wd_stuck_s`). First nudge: raise
  the drone by 300 uu through `DriveMirror`-style `SetActorLocation` on the host (the stream carries it). If still
  stuck after another 45 s, do a **hard reset**:
  - teleport to `sellLocation` (`src/ue_wrap/world/order_economy.cpp:71-77`);
  - write `Active=0`, `canTakeOff=0`, `hasSack=1`;
  - if `saveSlot.orders.Num>0`, call `checkOrders` (risky `[?]`: run it first under `drone_probe` to confirm it is
    the right resume verb).
- Put it all behind ini `drone_watchdog=1`, default on, with per-action log lines so user reports become diagnosable.

**F2. Make the sack host-authoritative. High confidence, medium effort. Fixes S1 and S2 at the root.**
- **(a) Drop.** Client-gate `drone_C.actionOptionIndex` for the drop-sack option. The option index comes from
  `getActionOptions` `[?]`; probe it the way `drone_call_intent` does for its options. Send it as a new intent verb on
  `DroneFlyIntent` (e.g. `drone_verb::kDropSack`). The host runs `dropSack` on its drone after a reach check. The sack
  birth then crosses on the normal host birth path. Leave "open inventory" local: it only opens the shared container,
  which the container lane already arbitrates.
- **(b) Return.** Add the drone's sack-entry overlap (function name `[?]`: probe `drone_C`'s
  `ReceiveActorBeginOverlap`/`BndEvt*` with `drone_probe`) to `mirror_slot_entry`, but with the *inverse* rule: **on
  clients always refuse it, on the host always allow it**, even for a sack a remote player carries. The client's
  release becomes a drop intent (`docs/props.md:96-98`). The host's placed copy overlaps the host drone, the host takes
  it, the destroy relays and `hasSack=1` streams back. If the overlap only fires on release, the host-side
  carried-mirror case also works, because the host is allowed.

**F3. Feedback for refused presses. Easy, low risk.**
- In `Execute` (`src/coop/interactables/drone_call_intent.cpp:138-175`), on DENY (lid, reach) send a short refusal back
  to the sender. Also send one when `triggerFly` ran but `Active`/`canTakeOff` did not change within 2 s.
- The client then shows a HUD/chat line such as "the lid is shut on the host" or "the sack is missing: press again",
  so the client knows to press again.
- Log `hasSack` / `canTakeOff` / `sackInWorld` on every press.

**F4. Re-suppress per drone identity. Trivial.**
- Replace `bool suppressed` with a `CachedObjRef` of the suppressed drone (`src/coop/interactables/drone_sync.cpp:52,182,316`)
  and re-suppress whenever `D::Find()` returns a different live actor.

**F5. Neutralise the mirror's latent continuations (S5). Optional.**
- On clients, gate `beginFly`, `dropSack`, `checkOrders` and `compileOrder` on `drone_C` at the script gate (Cancel
  unless the call comes from the mod's own apply), the same way `sendShop` is already refused
  (`src/coop/items/order_sync.cpp:201,233-235`).

**Verification:** `drone_call_drill=1` (`include/coop/dev/drone_call_drill.h:1-15`) already walks a client to the console.
Extend it with (i) a client sack round-trip (drop on host, client carries it back, assert host `hasSack=1` within 5 s)
and (ii) a forced obstacle on the pad to trigger the watchdog.

---------------------------------------------------------------------------------------------------
## Part B — The "small Claude" figurine

### B1. What exists today `[V]`

- **Shipped pak pipeline.** `assets/paks/scientists.pak` (2.7 MB, Zlib) contains cooked skeletal meshes and textures
  under `/Game/Mods/VOTVCoop/<name>`, built by repak V11 (`assets/paks/README.md:38-47`).
- **The source tool is not in the repo.** The paks come from an external "converter" that splices GoldSrc models into
  the cooked `kerfurOmega_KelSkin` template (`assets/paks/README.md:56,76`; `include/coop/player/client_model.h:3-9`;
  `docs/players.md:121-128`). There is **no UE editor project, no `.uproject` and no `.uasset` sources** in the repo;
  `assets/` holds only branding and the pak with preview PNGs.
- **Mounting and loading.** Paks dropped in `LogicMods` (Thunderstore: `shimloader\pak`) auto-mount at startup
  (`include/coop/player/model_folder.h:3-8,14`). Assets load through the game's `RyRuntimeObjectHelpers.LoadObject`
  (`include/ue_wrap/core/asset_load.h:3-20`).
- **Runtime re-skin primitives already wrapped:**
  - `CreateDynamicMaterialInstance`, `SetTextureParameterValue`, `SetScalarParameterValue`, `SetStaticMesh`,
    `SetComponentMaterial` (`include/ue_wrap/engine/engine_component.h:63-100`);
  - deferred spawn `BeginDeferredSpawn`/`FinishDeferredSpawn` (`include/ue_wrap/engine/engine.h:62-67`);
  - runtime components via `scs_rig` (`include/ue_wrap/engine/scs_rig.h:21-35`).
  The skin system already does a MID swap of `'tex'` onto slot 0 (`src/coop/player/client_model.cpp:189-194`).
- **Prop spawning that syncs.** A host spawn of a class path is broadcast by the finish-spawning seam
  (`docs/props.md:41`). An example is a dev drill spawning `/Game/objects/prop_eriePlush.prop_eriePlush_C` with
  `LoadObjectByPath` + `DropOnFloor` (`src/coop/dev/hostile_drill_act_family.cpp:49`,
  `src/coop/dev/hostile_drill_act.cpp:517-529`).
  - A prop's look is its `list_props` row `Name`, which crosses in `PropSpawnPayload.propName`
    (`include/coop/net/protocol.h:1911-1917`).
  - Its persistent `Key` also crosses (`:1912`). A Key set with `setKey` between Begin and Finish survives `Init()`
    (`include/ue_wrap/core/sdk_profile_names.h:547-551`; auto-mint happens only when Key is None,
    `include/coop/props/prop_synth_key.h:4`).
  - Clients cannot author new shared props (`docs/props.md:90-111`), so the host must spawn it.
- **Spawn UI.** "Content > Entities" is NPC spawns (`src/ui/dev_menu.cpp:573-578`, `src/ui/dev_panes.cpp:202-240`).
  "Content > Props" is the Q-menu unlock (`src/ui/dev_panes.cpp:252-268`). Both are dev-gated
  (`coop::dev_gate::Allowed()`).
- **Game content checked (read from the pak index tail, nothing unpacked):**
  - plush and figure classes: `prop_kelplush`, `prop_kelplush1`, `prop_eriePlush`, `prop_samplush`, `prop_dreamplush`,
    `prop_abplush`, `prop_moniquePlush`, `prop_monikaPlush`, `prop_jollyplush`, `prop_furfurPlush`, `prop_grayFigure`, `prop_watoy`;
  - plus static meshes and textures for generic rows (`zaxplush`, `berdplush`, `igpPlush`, `ayyLmaoFigure`, `figura` ...).
- **Runtime facilities checked in `VotV-Win64-Shipping.exe` (string scan):**
  - `ImportFileAsTexture2D` and `ImportBufferAsTexture2D` (UKismetRenderingLibrary, PNG → UTexture2D at runtime);
  - `ProceduralMeshComponent` + `CreateMeshSection_LinearColor` (the procedural-mesh plugin is compiled in);
  - `BytesToImage`, the laptop's PNG decoder (`src/coop/interactables/signal_wire.cpp:28-30`).
  - `[?]` Confirm each one at runtime with `FindClass` / `FindFunction` before relying on it.

### B2. Identity: how every peer knows which prop is "Claude"

Pick one:
- **(i) Key prefix (recommended).** The host spawns with `setKey("claude_fig_<guid>")` before Finish. The Key is saved
  by the game, crosses the wire, and is kept across pickup/drop in coop ("the key is parked", `docs/props.md:94-98`).
  Every peer's figurine module dresses any prop whose Key starts with `claude_fig_`. It hooks the same birth/scan seams
  the prop pipeline uses (`coop/element/object_scan_hub`, Init POST, the remote spawn). No protocol change, and
  persistence comes free.
  - `[?]` Whether single-player inventory round-trips keep the Key needs one check.
- **(ii) Row hijack.** Every instance of one rarely seen plush row becomes Claude. This is the least code, but that
  plush is lost.
- In both cases, a peer without the mod or the asset sees the stock plush. Nothing breaks and saves stay vanilla-loadable.

### B3. Options, with effort

| # | Approach | Visual | Editor? | Effort | Risk |
|---|---|---|---|---|---|
| **A** | Spawn an existing plush (e.g. `prop_kelplush_C`). On every peer, `CreateDynamicMaterialInstance(mesh,0)` + `SetTextureParameterValue` with a texture from `ImportFileAsTexture2D("<moddir>\claude_plush.png")`, or `ImportBufferAsTexture2D` from PNG bytes embedded in the DLL. | the plush's shape in Claude terracotta (#D97757) with a sparkle face | no | **0.5–1 day** | The plush material's texture parameter name is unknown `[?]`. If it has none, swap the slot to a material known to have `'tex'` (the KelSkin MI the skins use) and paint the PNG in the plush's UV layout. That needs one UV export via FModel/umodel. |
| **B** | Spawn an existing small plush as the physics/save carrier, hide its mesh (`SetVisibility(false)`, collision kept), attach a runtime `ProceduralMeshComponent` with a C++-generated **extruded rounded 8–12-ray starburst** (the Claude mark). Material: a MID of a known lit material, colour from a 4×4 terracotta PNG via `ImportBufferAsTexture2D`. | a real Claude sparkle-star figurine | no | **2–3 days** | The procedural-mesh class must load (`[?]`, string present). Lighting and normals need care. One extra component per figurine; re-dress after every re-spawn or mirror birth. |
| **C** | Model a cute Claude plush or star in Blender. Cook it with a **free UE 4.27 editor** project laid out like the mod's (`/Game/Mods/VOTVCoop/claude_fig` StaticMesh + MI). Pack with the same repak command as `scientists.pak` (`assets/paks/README.md:46`). At runtime `LoadObjectByPath` + `SetStaticMesh` on a spawned plush carrier (identity per B2). | best (custom mesh and texture) | yes (cook only) | **2–4 days** incl. editor install and a first cook | Peers need the pak (ship it in `pak\`, like the scientists). The collision is still the carrier's. |
| D | A new BP class `prop_claude_C` with a `list_props` row | best and native | yes | 4+ days | `list_props` cannot be extended without replacing the asset or injecting rows at runtime. Saves break for players without the mod. **Not recommended.** |

**Recommendation:**
1. Ship **A + B2(i)** first. It is quick and proves the identity, dressing and sync loop.
2. Upgrade the visual to **B**, the procedural star, so there is no new asset pipeline.
3. Use **C** only if a hand-modelled plush is wanted.

The spawn control should be a host-only button: "Spawn Claude figurine (in front)" in Content > Props. The pane is
dev-gated (`src/ui/dev_panes.cpp:253`), so for ordinary players add a non-dev host entry. The button calls
`BeginDeferredSpawn(prop_kelplush_C)` → `setKey("claude_fig_<guid>")` → `FinishDeferredSpawn`. After that the
ordinary prop lanes carry it.

Code sketch of the module `coop/props/claude_figurine.cpp`:
- `Install`: resolve the plush class, `ImportBufferAsTexture2D`, and for option B the material and mesh data.
- `OnPropSeen(actor)`: if the Key has the prefix → `Dress(actor)`. This is idempotent: it keeps a per-actor
  `CachedObjRef` set and re-dresses after a world change.
- `Dress`: the MID texture swap (A), or hide the mesh plus attach the procedural mesh (B).
- `SpawnInFront()`: host only.
