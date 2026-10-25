# Bug #4: clients can't use the cremator, transformers (generators) or mining farm

Scope: a connected client's interaction with (a) the basement furnace (in this code the **cremator**,
`cremator_C`), (b) transformer repair (the **generators**: `generator_C` plus the puzzle panel
`transformerMGPanel_C`, and the breakers on `powerControl_C`), (c) the mining farm (`prop_miner_C`).
All paths are relative to `WatchersOfTheVoid-main/src/votv-coop/` unless they start with `agent/`, `docs/` or `WotV/`.
`[V]` means verified from code (or git history). `[?]` means a hypothesis that needs the game's Blueprints or a runtime log.

---

## 0. TL;DR

| Device | Lane exists? | Why the client's action "does nothing" | Confidence |
|---|---|---|---|
| Transformers (generators) | Yes: `coop/world/power_grid` plus `power_puzzle`, `power_upgrade` and `power_decay` | **All 8 generator watches are registered on a class that does not exist**, `L"powerGenerator_C"`. The real class is `generator_C`. So the client's Activate press, upgrade install, hit and repair-door are never sent. The client's local repair runs only on the client, and the host's next rows **re-break it** (`ClientReconcile` → `CallBreak`). The bug came in with the merge `47a4fc7d4`. | **[V] high** |
| Cremator (basement furnace) | Yes. The door goes over `kitchen_sync` (KitchenIntent op 2/3/4). The lever goes over `door_verb_intent` (DoorVerbIntent, action 4). Burning is host-only through `spawn_authority`. | The lever is forwarded only when `CanUseLever` is true, and that needs **both** `lookingAt_info` and `lookingAt_handle`. Otherwise the press runs **locally** as a paint-only replica (no burn, host never told). Any cremator action other than 4 also runs locally and is never forwarded. Burning needs the host's copies of the props to be inside its cremator, which bug #1 (prop desync) breaks. | [V] mechanism / [?] which case hits in game |
| Mining farm (`prop_miner_C`) | Partly: `coop/world/credit_intent` forwards **only action 4** (token claim) | Every other client action on a miner (and any `playerUsedOn`) runs only on the client and never reaches the host. The host runs the claim with **the host's own player** as `player`. Nothing waits for the sender's body (`InReach` NoBody means a silent drop). The lane hasn't changed since b2142, where it was "reported working". | [V] mechanism / [?] which action the user means |

Not involved: `coop/permissions`. The only gameplay node is `multivoid.gameplay.toolgun.remove`
(`include/coop/permissions/gameplay_nodes.h:22-23`) [V]. No AnyClass `actionOptionIndex` watcher
cancels these classes either: toggle_verbs is post-only (`src/coop/interactables/toggle_verbs.cpp:115`). sleepingbag,
coingun, hook_retrieve and kitchen_sync are class-gated (`sleepingbag_sync.cpp:104-116`, `coingun_collect.cpp:336`,
`hook_retrieve.cpp:101`, `kitchen_sync.cpp:405-406`) [V].

---

## 1. The general mechanism (client interaction: run locally, refuse, or forward)

1. **The seam.** Every Blueprint body goes through the VM script loop, and `ue_wrap/core/script_gate` detours it
   (`include/ue_wrap/core/script_gate.h:1-11`). A lane registers a PRE (returns Run/Cancel) and/or a POST:
   - `WatchName(name)`: AnyClass, matches any class's function with that name (`script_gate.h:88-100`).
   - `WatchClassName(cls, name)` / Declared row: fires **only when the body's owning class short name == cls**.
     It's checked at call time by `OtherOwner` (`src/ue_wrap/core/script_gate_detail.h:78-82`, used at
     `src/ue_wrap/core/script_gate.cpp:170`). A wrong class literal is never "dead" or logged at call time. It just never matches.
   - Every matching entry fires, and **any Cancel cancels** the body (`script_gate.cpp:159-182`, `:245-247`).
2. **Interaction entry points** (the game's interact BPI) [V]:
   - `actionOptionIndex(player, hit:FHitResult, action:uint8, lookAtComponent)`, the E/option press
     (`src/ue_wrap/desk/coord_tower.cpp:331-333`, `src/ue_wrap/devices/generator.cpp:287-289`).
   - `playerUsedOn(player, hit, lookAtComponent, holdObject)`, using a held item on the object
     (`generator.cpp:301-303`).
   - Most BPs branch on **per-actor look-at fields** that `getActionOptions` writes for the local trace
     (`lookAtButton`, `lookAtUpgrade`, `lookingAt_handle`, the tower's look). A host replay has to write
     those fields before the call and restore them after (`coord_tower.cpp:323-336` SaveLook/WriteLook/RestoreLook,
     `generator.cpp:285`).
3. **The three outcomes on a client PRE:**
   - *Run*: no lane, or the lane decides it's local. The body runs on the client's copy only. Unless a
     symmetric state lane (toggle_verbs, appliance, oven fix) sends the result, the host never knows.
   - *Cancel + send an op/intent*: e.g. `door_verb_intent.cpp:381-391`, `credit_intent.cpp:175-186`,
     `kitchen_sync.cpp:404-424`, `coord_tower_ops.cpp:380-405`. The host routes it by ReliableKind
     (`src/coop/dispatch/event_dispatch_intent.cpp:56`, `:783`, `event_dispatch_world.cpp:308`, `:458`),
     re-checks reach with `IntentTarget::ForClientIntent(...).Authorize(actor)`
     (`include/coop/element/intent_authority.h:88-113`) and runs the same verb on its own copy, normally with the
     **sender's puppet** as `player` (`door_verb_intent.cpp:260-269`, `kitchen_sync.cpp:609-611`,
     `coord_tower_ops.cpp:230-235`).
   - *Cancel without sending (a refusal)*: e.g. the replica refusals in `spawn_authority` (`src/coop/world/spawn_authority.cpp:137-146`)
     and the power grid's own-edit refusals (`power_grid.cpp:530-541`).
4. **The result goes back** through a host-authoritative state lane: rows, KitchenState, ContainerState, or `prop_save_data`.

---

## 2. Transformers / generators (repair puzzle, Activate, upgrades, hits, repair door)

### 2.1 Lane design (from the docs)
`docs/devices.md:212-235` and `docs/status.md:62` say the generators are host-owned. A client's Activate press,
upgrade install, hit and puzzle inputs are ops (PowerGridState). The host sends rows, and the client reconciles.

### 2.2 The defect [V, high confidence]
- `src/coop/world/power_grid.cpp:674` registers every watch with `sg::WatchClassName(L"powerGenerator_C", w.name, ...)`, and the
  liveness checks at `:701-702` use the same literal. The watch table `:650-659` holds `openDoor`, `break`, `damage`, `fullFix`,
  `updUpgrades`, `upd`, `actionOptionIndex` and `addDamage`.
- The real class is **`generator_C`**:
  - `src/ue_wrap/devices/generator.cpp:100` resolves it with `ClassByName(L"generator_C")`, and the header calls it
    "Agenerator_C" (`include/ue_wrap/devices/generator.h:1`).
  - The 0.9.0n fingerprint table lists exactly these functions on `generator_c`
    (`include/ue_wrap/facts/fingerprints_0.9.0n.inc:163-169`).
  - The cooked asset is `objects/generator` with `ExecuteUbergraph_generator` (`agent/tests/blueprint_2126_test.py:13`).
  - `powerGenerator` appears nowhere else in the repo.
- Git (`WotV/` clone): both parents of merge `47a4fc7d4` ("Merge branch 'watchers-of-the-void-2150'") used
  `generator_C`. Parent 1 uses `UE_FACT_FN(kRowBreak, "power_grid", L"generator_C", ...)` at line 621ff, and parent 2 `852d667d6` uses
  `kGenClass[] = L"generator_C"`. The merge result introduced `powerGenerator_C`, so this is a **merge regression**.
- The unit test stubs `WatchClassName`/`ClassNameWatchLive` to return true (`agent/tests/power_door_2126_test.cpp:19-21`),
  so offline tests couldn't catch it. At runtime the lane never logs "the grid's gates are live" (`power_grid.cpp:704-707`).
  [?] It may also never log "dead", because no class named `powerGenerator_C` ever loads for `JudgeWaitingClasses` to judge
  (`src/ue_wrap/core/script_gate_names.cpp:214-238`).

### 2.3 What the client sees (follows from the code) [V]
1. The client opens the panel. Puzzle inputs **do** work: `power_puzzle` watches `transformerMGPanel_C` by Declared rows
   (`power_puzzle.cpp:226-243`), captures inputs (`:176-194`), flushes them each tick (`:331-332`), and the host takes them
   if the client holds the panel's occupancy claim (`:275-299`).
2. The client presses Activate. `OnActivatePre/Post` (`power_grid.cpp:579-608`) never fire, so **no
   `kPowerGridOpActivate` is sent**. The client's own Blueprint runs the repair locally (isBroken off, turnedOn, upd).
3. The host's generator stays broken. The host still broadcasts rows: every 500 ms if they changed (`:684`), and within 100 ms
   of each taken puzzle input (`:683`, `:340`). The client's `ClientReconcile` sees `e.broken(1) != r.broken(0)` and
   calls `GEN::CallBreak` (`:478-487`). **The client's repair is undone and its base blacks out again.** That matches
   "client actions do nothing".
4. Upgrade installs: `OnUpdUpgradesPre` (`:562-576`) is dead, so no op is sent. The spent upgrade prop is still destroyed through the prop
   lanes, the next rows put the level back (`:497-501`), and **the upgrade is lost**.
5. Hits: `OnHitPre` (`:612-631`) is dead, so the client breaks its own copy, and the rows repair it back.
6. The repair door (`openDoor`, `:633-642`) is dead too, so the CHANGELOG feature "Transformer repair door"
   (`package/CHANGELOG_PLUS.md:416`) can't work for clients. The host's state still reaches them through the 500 ms row poll.
7. Side effect: `OnOwnEditPre` (`:530-541`) is meant to refuse the client's own break/damage/fullFix, but it never runs.
   So every client's local decay or saboteur effects also run, and only the next rows put them back.

Breakers (`power_panel`) use `powerControl_C` / `ui_breakerComp_C` rows (`power_panel.cpp:415-420`). Those match the
fingerprints (`powercontrol_c`, `ui_breakercomp_c`), so they're probably fine. Not audited further.

### 2.4 Fix (rank 1: highest confidence, smallest blast radius)
- `src/coop/world/power_grid.cpp:674,675,701,702`: replace `L"powerGenerator_C"` with `L"generator_C"`, or better, move to
  `UE_FACT_FN(..., L"generator_C", ..., Declared)` rows the way parent 1 did, so the fact or fingerprint gate validates the class.
- Add a guard: if a class-scoped watch hasn't settled N s after `GEN::EnsureResolved()` succeeds, log an ERROR. Also make
  the selftests assert the literal that `WatchClassName` receives. For example, `power_door_2126_test.cpp` could record the class arg and
  check it equals `"generator_C"`.
- Expected log after the fix: client `power_grid: local Activate press of generator N -- sent to the host`, then host
  `power_grid: host took slot S's Activate press`.
- Sweep for the same failure: grep every `WatchClassName(L"...")` / Declared row and check the class against
  `fingerprints_0.9.0n.inc` or a live `ClassByName`. The merge changed about 60 class literals
  (`git diff 47a4fc7d4^1 47a4fc7d4 -- src`).

---

## 3. Cremator (basement furnace / incinerator)

### 3.1 What exists [V]
- Wrapper: `src/ue_wrap/devices/cremator.cpp`. The lever is `actionOptionIndex(action=4) -> useLever()` (`include/ue_wrap/devices/cremator.h:11-12`).
  `CanUseLever` needs `anim==false && lookingAt_info && lookingAt_handle` (`cremator.cpp:84-88`). `CanReplayLever` needs
  only `anim==false` (`:90-93`). The key is `PortableWireKey + "/lever"` (`:109-113`), which uses the actor_save key
  (`src/coop/element/portable_identity.cpp:63-65`).
- **Lever, client side** (`src/coop/interactables/door_verb_intent.cpp:309-333`, the AnyClass `actionOptionIndex` watch at `:44`, `:397`):
  for a cremator with tag DVPR, an in-world object, action == 4 **and** `CanUseLever`, it sends a DoorVerbIntent and Cancels. If the key is
  empty it Cancels and sends nothing (`:320`). **In every other case it returns Run** (`:316-318`).
- **Lever, host side**: `Execute` (`:171-201`) checks reach (400 uu + bounds), action == 4 and `CanReplayLever`, calls
  `useLever` directly, and publishes `isActive` over ContainerState (`:106-132`). A 200 ms poll republishes any change (`:433-447`).
  A client applies the change by running its own `useLever` (`:134-146`, `:449-453`, `:501-517`).
- **Burning is host-only**: on clients, `spawn_authority` refuses `cremator_C.collectObjects`, `igniteInside`,
  `removeTrash` and `spitFireballs_timer` (`src/coop/world/spawn_authority.cpp:124-128`, `:137-146`). The comment there says "Replicas paint lever/door
  state but never consume or ignite contents".
- **Door**: `kitchen_sync` watches `Open`/`Close` by name. On the client it cancels them and sends KitchenIntent op 2/3 (`kitchen_sync.cpp:433-444`),
  and the mouse drag goes as op 4 (`:445-463`, `:516-536`). The host checks reach at 500 uu and runs `SetDoor`/`MoveDoor` (`:579-619`).
  `K::Door`/`DoorParent` cover `Kind::Cremator` (`src/ue_wrap/devices/kitchen_machine.cpp:111-128`).
- Cremator actions in `kitchen_sync::OnAction` are passed through: `if (K::Type(c.object)==K::Kind::Cremator) return Run;`
  (`kitchen_sync.cpp:406`). The host also drops cremator op 1 (`:587`).

### 3.2 Why a client's furnace use can do nothing
1. **[V mechanism, ? trigger]** The lever is forwarded only when `lookingAt_info && lookingAt_handle` are both true on the
   client's copy (`cremator.cpp:86-87`). The wrapper's own header says the BP gate is "lookingAt_handle and anim"
   (`cremator.h:11-12`), so `lookingAt_info` is an extra condition. If it's false (for example it only means "looking at the
   info plate"), or `anim` is true, the client **runs `useLever` locally**. The client is a replica, so its burn functions are refused
   (`spawn_authority.cpp:137-146`), the host is never told, and a later host poll puts `isActive` back. Result: the lever moves, nothing burns.
2. **[V mechanism, ? which actions exist]** Any cremator `actionOptionIndex` action other than 4 runs on the client only.
   Neither `door_verb_intent` (`:313`, `:317`) nor `kitchen_sync` (`:406`) forwards it. If the furnace has a second control
   (an ignite/power button, a hatch or tray action), that control is client-local and its burn path is refused there.
3. **[?] Contents.** The host burns the props that **its** copy finds inside (`collectObjects` runs only on the host). Trash a
   client threw in is only inside the host's cremator if prop motion converged (bug #1), or if the client released it as a
   host-owned prop. Otherwise the host burns nothing, or burns props the client doesn't see there.
4. **[V] Silent host refusals.** If the host's `anim` is true, or the sender is out of reach, `++g_denied` is the only result. There is no answer to
   the client and no log (`door_verb_intent.cpp:178-181`). `NoBody` waits (`:177`). The door intent drops silently while the host's own
   player is the door's user (`kitchen_sync.cpp:599`).
5. **[V] Dead code.** `g_replayingCremator` / `CrematorReplayGuard` (`door_verb_intent.cpp:95-104`) is written but never read, so
   it guards nothing. That's harmless today, but it suggests a client-side "apply vs. press" distinction was intended and never built.

### 3.3 Fixes (ranked)
- **C1 (high confidence, low blast radius).** In `door_verb_intent.cpp:315-318`, a client cremator press with action == 4 that is in-world
  should always Cancel and forward. Drop the `CanUseLever` precondition, or require only `lookingAt_handle`, not `lookingAt_info`.
  The host already checks `CanReplayLever` and reach. Also log the refusal reasons in `Execute` (`:178-183`), and send the host's
  current lever state back to the sender on a refusal (`PublishCrematorLever(cremator, active, slot)`) so the client's replica converges.
- **C2 (medium).** Forward the cremator's other `actionOptionIndex` actions through the generic lane in section 5 (host runs it with the
  puppet and the look-at copied), instead of `return Run` at `kitchen_sync.cpp:406`.
- **C3 (medium).** For contents: when a client releases or throws a prop near or into a cremator, push an authoritative pose for it to the host
  (bug #1 owner). Or, on a client lever press, send the eids of the props the client sees inside, and let the host teleport its copies in
  before `useLever`. That second option needs `prop_element_tracker`, and the host must refuse non-overlapping eids.

---

## 4. Mining farm (`prop_miner_C`)

### 4.1 What exists [V]
- `src/coop/world/credit_intent.cpp:497` adds a class-scoped watch on `prop_miner_C::actionOptionIndex`.
- Client PRE `OnMiner` (`:175-186`): **only action 4** ("the rack laptop's token-claim action") is cancelled and sent as
  CreditIntent/MiningFarm. Every other action returns Run, which means local only.
- Host `CreditMiner` (`:445-450`): it resolves by prop key or eid (`:122-131`), checks reach at 500 uu (`:133-136`), and calls
  `RunAction(miner, 4)`, which sets `player = Registry::Get().Local()`, **the host's player** (`:354-362`). Host POST
  `OnMinerPost` (`:161-173`) ticks `balance_sync` and publishes the miner's `prop_save_data` record.
- History: `credit_intent.cpp` is byte-identical to the b2142 Plus import (`git diff 852d667d6 HEAD` doesn't list it), and
  report 6 "Mining farm: Reported working" (`agent/history/TESTLOG-2.md:202`, `:290`). The maintainers marked it closed
  (`agent/TODO.md:76`, `:653`).

### 4.2 Why a client's farm use can still do nothing
1. **[V] Only the claim is forwarded.** Other miner actions (power toggle, GPU/part insert, upgrade and so on) run on the client only:
   `OnMiner` returns Run (`:178`), and no `playerUsedOn` watch exists for `prop_miner_C`. Each peer then mines on its own
   copy, and the host's copy (the only one whose claim credits the balance) has none of the client's changes. The only miner state
   that crosses is the host's `prop_save_data` after a host-run claim (`:170-171`). A client's local change is never published, and
   the host's next record overwrites it.
2. **[?] The claim runs as the host's player** (`:359`). If the BP's action 4 opens a UI or a laptop screen for `player`, gives an item,
   or spawns output at `player`, the result happens on or at the **host**, and the client sees nothing.
3. **[V] No wait for the body, and no answer.** `InReach` → `Authorize` drops the intent silently on `NoBody` or out-of-reach (`:448`).
   Compare door_verb's wait (`door_verb_intent.cpp:177`).
4. **[V] Class-scoped watch.** If a miner variant is a subclass that overrides `actionOptionIndex`, `OtherOwner`
   doesn't match it (`script_gate_detail.h:78-82`), and that subclass's claim runs locally. `prop_miner_C` isn't in
   `fingerprints_0.9.0n.inc`, so its class name and declared functions can't be checked offline [?].
5. **[?] Power.** Miners presumably need base power. With the generator lane dead (section 2), the client's local power can
   diverge (local repair then re-break), which changes what the client's miners show.

### 4.3 Fixes (ranked)
- **M1 (medium confidence, low blast).** Run `CreditMiner` with the sender's puppet as `player`, as `door_verb_intent`
  and `kitchen_sync` do. Add a NoBody wait queue, and log refusals.
- **M2 (medium).** Forward all miner `actionOptionIndex` actions, and `playerUsedOn` with a spend pairing like `power_upgrade`'s
  (`include/coop/world/power_upgrade.h`), through the generic lane in section 5. After each host-run action, call `prop_save_data::Publish`
  (as `OnMinerPost` does now) so every peer takes the host's miner record.
- Ask the user which action fails (claim, power on, insert) and get a client `multivoid.log` with
  `credit:` lines. `CreditMiner` logs nothing on success, so add an `UE_LOGI` there.

---

## 5. Generic "forward unhandled interaction to host" lane (design)

Existing lanes to copy from: `door_verb_intent` (queue, rate, NoBody wait, puppet as player), `coord_tower_ops` plus
`coord_tower.cpp:323-336` (look-at save/write/restore around a host `actionOptionIndex`), `credit_intent` (key/eid
identity, `RunAction`), and `prop_save_data` (result broadcast).

**Client PRE** (one AnyClass watch on `actionOptionIndex`, optionally also on `playerUsedOn`):
- It acts only if all of these hold: client session; `!fromOurCode`; `net_pump::IsInAnnouncedWorld(obj)`; `player` param `IsLocal`
  (as `sleepingbag_sync.cpp:107-115` checks); and the object's class is in an **explicit allowlist** (`cremator_C` non-lever actions,
  `prop_miner_C`, ...). Don't default to every class: many BPs open local widgets or move items into the local hand, and
  those need dedicated lanes (prop_act, laptop, notebook).
- Identity: `prop::GetInteractableKeyString`, then `GetActorSaveKeyString`, then `PortableWireKey`, then eid (the fallbacks used by
  `credit_intent.cpp:113-131` and `kitchen_sync.cpp:122-151`).
- Payload: identity, `action`, `lookAtComponent`'s FName (component names are unique per actor:
  `portable_identity.h` "Uniqueness is STRUCTURAL"), the hit location relative to the actor, and a snapshot of the object's
  look-at fields (every bool/byte/int/object property whose name starts with `look`, read by reflection).
- Return Cancel only if the send succeeded. If it didn't, Run, as sleepingbag does (`sleepingbag_sync.cpp:116`).

**Host:**
- Per-slot deque with an `IntentBucket` and a NoBody wait (copy `door_verb_intent.cpp:148-159`, `:459-468`).
- Resolve the actor, find the component by name, then `IntentTarget::ForClientIntent(s, slot, 400).Authorize(actor)`.
- Save the host's look fields, write the client's snapshot, and call `actionOptionIndex(puppet, hit_result::Write(f, L"hit", actor, comp, at),
  action, comp)` (exact shape: `coord_tower.cpp:331-333`). Then restore the look fields.
- After the body: if `prop_save_data::Covers(actor)`, call `Publish` so every peer takes the host's record. Otherwise the class needs a state lane.
- Answer refusals to the sender, so its replica can resync.

**Risks:** the BP may read the player's local state (hand, inventory, camera trace) in its body. In that case the replay acts on the puppet
(empty hand) or must be refused. Two lanes must never handle one class, so dedicated lanes keep their classes and the allowlist excludes them.

---

## 6. Recommended order
1. **Fix the `powerGenerator_C` → `generator_C` typo** (`power_grid.cpp:674,675,701,702`). Highest confidence; it alone
   explains "can't repair transformers" and lost upgrades.
2. Cremator C1 (always forward lever action 4; log host refusals and answer them).
3. Miner M1 (puppet as player, NoBody wait, logs), then confirm with the user which miner action fails.
4. The generic allowlisted lane in section 5 for the remaining cremator and miner actions.
5. A CI or selftest guard that checks every class-scoped watch literal against the fingerprint table.

Runtime evidence to collect (client and host `multivoid.log`):
- `power_grid: the grid's gates are live`. Expected to be **absent** today.
- `[DOOR-VERB] CLIENT SENT cremator lever press` and `host ran slot N's cremator lever`.
- `spawn_authority[cremator.*]: client-refuse`. If this appears when the client pulls the lever, it proves case 3.2-1.
- `credit:` lines.
