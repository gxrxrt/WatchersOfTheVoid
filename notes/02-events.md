# 02 -- Event desync: event runs on host, nothing appears on client

Scope: story/scheduled/random events. Source tree: `WatchersOfTheVoid-main/src/votv-coop`
(paths below are relative to `src/votv-coop/src/` unless they start with `docs/`, `agent/` or `include/`).
Tags: [V] verified from code, [?] hypothesis that needs a log line or a probe.

## 1. Summary of the mechanism

An event reaches a client through one of four routes. Each route only covers a specific kind of output.

1. **Eventer verb watch** (`coop/world/event_fire_sync.cpp`).
   - Host side: script-gate name watches on `trigger_eventer_C::runEvent` and `runSpecialEvent` (`:65-66`, installed `:571-575`) broadcast the row name after the body runs (`:342-374`).
   - Client side: the client looks the row up in a per-row policy (`coop/world/event_fire_policy.cpp:10-102`). A row is replayed natively only if it is in `kReplayRows`. Every other row is dropped (`event_fire_sync.cpp:669-678`). This includes unknown rows (verdict -1).
   - The client's own scheduler is emptied before `saveSlot_C::settime` runs (`coop/world/event_schedule_authority.cpp:29-67`). Any eventer call on the client that the replay did not admit is cancelled at the gate (`event_fire_sync.cpp:255-272`). [V]
2. **Spawn lanes for the actors an event spawns.**
   - NPCs: the PE interceptor (`npc_sync`) plus the EX_CallMath catch (`coop/creatures/npc_world_enum.cpp:191-226`).
   - World actors: the PE interceptor (`coop/world/world_actor_sync.cpp:233-318`) plus the same EX catch, drained to `HostEnrollExSpawn` (`npc_world_enum.cpp:263-269`).
   - Props: the `FinishSpawningActor` native seam (`coop/props/host_spawn_watcher.cpp:251-283`).
3. **Cue/sky/weather lanes**: `event_cue_sync` (starRain only, `event_cue_sync.cpp:63-65`), `sky_sync`, `weather_*`, `world_sound_sync`, `alarm_sync`, `ariral_ship_sync`.
4. **Join snapshot** (`event_active_sync`, class to row map `:89-115`), which re-sends in-flight rows.

**There is no generic fallback for non-prop actors.**
- The prop lane is generic: any `Aprop_C` descendant finished on the host is mirrored (`host_spawn_watcher.cpp:261`). [V]
- NPCs and world actors are name allowlists (`include/ue_wrap/core/sdk_profile_names.h:110-139`, `:151-178`). [V]
- The EX catch has two gates. The **source** object's class must be one of 17 listed spawners (`npc_world_enum.cpp:140-158`, gate `:204-206`), and the output must be on one of the two allowlists (`:210`). Anything else returns silently: no log, no mirror. [V]
- The client receiver also refuses any class name that is not on the world-actor allowlist (`coop/world/world_actor_mirror.cpp:93-98`). [V]

## 2. Event table: every row the mod knows, its policy, and what the client actually gets

Row list source: the dev menu table `coop/dev/event_trigger.cpp:56-190`. It has 65 runEvent rows, 30 special rows, one prank and the ambient verbs. A script diff found every eventer row in that menu present in the policy. The only names the policy has that the menu does not are the `arirInteraction_*` prefix rows (`event_fire_policy.cpp:90-92`). [V]

The native table has 69 scheduled rows (`agent/WORK.md:161`). That leaves about 4 rows unnamed in the source; they fall to verdict -1, i.e. no replay. [?]

Legend for the "Client sees" column:
- OK: a lane carries the output.
- PARTIAL: something arrives but visuals or behaviour are missing.
- **NONE**: nothing reaches the client.

| Row | Disp. | Policy (file:line) | Real carrier | Client sees |
|---|---|---|---|---|
| treehouse_0..5, break_RomeoSierra/Victor/Victor2 | RE | replay `policy:12-13` | native replay | OK [V]. Server-break bodies are refused on the client by the serverbox gate; state comes from the serverbox lane (docs/coop-dispatch-visibility.md row 144) |
| obelisk | RE | replay `policy:14` + location row (`event_location_sync.cpp:31`) | replay | OK [V] |
| looker_0-1..4-1, arirSignal, arirSpk, picSignal, peace, arirSat_0..2, piramid_sig | RE | replay `policy:16-18` | replay (forceObjects append) | OK [V] |
| solar, call0 | RE | replay `policy:21` | replay | OK [V] |
| toeStab, falseEnter, mann, vent, crys, fakeGrays, susArir | RE | replay `policy:24` (per-viewer arm) | replay | OK by design [V] |
| arirGraff_0..6 | SP | replay `policy:26-27` | replay (decal) | OK [V] |
| starRain | RE | no-replay `policy:33` | event_cue lane | OK [V] (`event_cue_sync.cpp:158-184`) |
| arirFollower | RE | no-replay "npc lane" `policy:34` | The follower is spawned by `trigger_spawnFollowingArir_C` (`event_active_sync.cpp:98`), which is **not** an EX source (`npc_world_enum.cpp:140-158`) | **NONE?** [?] Only correct if that spawn happens to be PE-visible |
| wisps | RE | no-replay `policy:38` | `trigger_wispSwarm_C` is a source | OK [V] |
| piramid | RE | no-replay `policy:43` | `piramidSpawner_C` source + piramid_sync | OK [V] |
| arirShip | RE | no-replay, policy itself says "(no lane)" `policy:47` | `arirShip_C` is world-actor allowlisted (`sdk_profile_names.h:158`), but its spawner (the trigger's overlap) is not a source | **NONE** [V] for "no lane" |
| earthTp | RE | no-replay `policy:48` | sky lane | OK [V] |
| vehtp / bedEvent | RE | no-replay `policy:49-50` | ATV / sleep lanes | OK [V] |
| picnic, destroyPicnic, enasus, enacros | RE | no-replay "prop lane" `policy:51-52` | FinishSpawning prop seam | OK [V] |
| cookier, paperGray, arirEgg | RE | no-replay "armed prop" `policy:53-54` | The prop exists on both peers, but the **arm flag** is set only on the host | PARTIAL [?]: the scare/overlap effect happens only on the host |
| console, lightswitch, keypadGuess | SP | no-replay "device lanes" `policy:55-56` | light/keypad lanes carry state; `event_consoleWrite_C`'s typing has no lane | PARTIAL [?] |
| atvExplode | SP | no-replay `policy:56` | ATV lane | OK [?] |
| agrav | RE | host-local by design `policy:58` | none | **NONE** by design [V] |
| treehouseSleep | SP | per-player `policy:59` | -- | by design |
| ventCrawler | RE | npc `policy:63` | eventer source + `ventCrawler_C` allowlisted | OK [V] |
| ventKnocker | RE | world actor `policy:63` | eventer + `kocker_C` | OK [V], pose only |
| tentacleBalls | RE | world actor + npc `policy:64` | follower is a world actor; balls come from the follower source | PARTIAL: the follower mirror's BeginPlay is **refused** (`spawn_authority.cpp:106`) |
| morningGay | RE | world actor `policy:65` | `morningUfo_C` | PARTIAL: pose only, actor tick off (`world_actor_mirror.cpp:243`) [?] |
| borgRozital | RE | world actor `policy:66` | `rozitBorg_C` | PARTIAL: pose only. Children spawned by the mothership are not caught (it is not a source) [?] |
| graysforest | RE | npc `policy:66` | `grayEventController_C` source -> `grayTest_C` | OK [V] |
| graystank | RE | "npc lane (grayTest_C)" `policy:67` | Actually the tank and pig droppers -> `firetankDeploy_C` -> `firetank_C` + grayboar props | PARTIAL: the dropper and deploy mirrors have BeginPlay refused (`spawn_authority.cpp:110-112`); the lane label is wrong |
| eggvasion | RE | `policy:68` | superEgger source -> `eg_C` | OK for the eggs; the egger's BeginPlay is refused (`spawn_authority.cpp:107`) |
| boarwar | RE | `policy:69` | `boarInvasion_C` source -> shooters, firetank; boars ride the prop lane | OK [V], pose only |
| soltoClean | RE | `policy:70` | `soltomiaCleaning_C` | PARTIAL: BeginPlay refused (`spawn_authority.cpp:108`); the sponge/door state is not carried |
| dreambase | RE | `policy:72` | `dreamer_dreambase_C` | OK [V] |
| fallbody_0/1, fallcar_0 | RE | world actor `policy:73-75` | The saucer is mirrored. Its **cargo** comes from the dropper's own BeginPlay; `ufoDropper_body_C`/`_car_C` are not EX sources; the client's BeginPlay is refused (`spawn_authority.cpp:109`) | PARTIAL / cargo **NONE** unless the cargo is an `Aprop_C` [?] |
| arirBuster | RE | "creature spawn (no lane yet)" `policy:67` | `arirBusterSpawner_C` is not on any list | **NONE** [V] |
| salt | RE | "save-actor spawn (no lane)" `policy:71` | `saltpile_C` is not on any list | **NONE** [V] |
| rozitalHole | RE | "save-actor spawn (no lane)" `policy:71` | `event_bottomHoleController_C` | **NONE** [V] |
| food, drive, atvFuel, atvFix, poisonFood, expDrive, cookiebox, trashPiles, bombBox | SP | "prank special (prop lane)" `policy:77-83` | props, via the FinishSpawning seam | OK [V] (the outputs are props) |
| vaccine, oil, begos, gascans | SP | "prank special (prop lane)" `policy:81-82` | They spawn `event_vaccine_C`, `oilStainSpawn_C` x8, `begoExplosion_C` x4 and `event_funnyGascans_C` (`event_trigger.cpp:142-145`). Unless those are `Aprop_C`, the prop lane does not carry them. begos may ride the explosion lane | PARTIAL/NONE [?] |
| rockThrow, hillRoller, alienJump, trashBase | SP | "prank spawner (no lane)" `policy:84-85` | `rockThrower_C`, `hillRollerSpawner_C`, `alienJump_C`, `arirTrasher_C` are not on any list | **NONE** [V] for the actors; thrown rocks or trash get through only if they are props |
| alienSounds | SP | world_sound_sync `policy:86` | audio only; the `noiser_C` movement is not carried | PARTIAL [V] |
| arirInteraction_* | RE | prank `policy:90-92` | the chosen case is sent as its own runSpecialEvent | per case above |
| (about 4 unnamed native rows) | RE | verdict -1 -> no replay, WARN `event_fire_sync.cpp:674-677` | -- | **NONE** [?] |

Events that do **not** go through the eventer:

| Event | Mechanism | Client sees |
|---|---|---|
| Black fog | The client kills its own birth (`coop/world/weather_event_births.cpp:41-44,61-73`) and there is **no host lane** (docs/events-and-weather.md:301) | **NONE** [V] |
| Flesh rain, Xmas gifts, walking tree, deer, hexahive, bp7, beehive, mannequin | each peer rolls its own (docs/events-and-weather.md:302-305) | a different event on each peer [V] |
| Unrouted UFO family (`ufo_boofo/joel/pillfo/trifo/ballfo`, `midasufotest`), `geomOcta_C`, `boneman_C`, `birch_C`, goat, fridge, poolwalker, `paranormalSpot_C` | not on any allowlist (agent/ENTITY_COVERAGE.md:43-79) | **NONE** if the host spawns them; otherwise per peer [V] |
| `skyUfo_C`, `skyFallingEvent_C` | world-actor allowlisted, but the spawner is not a source and the call is likely EX_CallMath | **NONE?** [?] |
| Red sky, Bad Sun, sky eye, jellyfish, rain/lightning/fog | dedicated lanes | OK [V] |

## 3. Root causes, ranked

### RC1 [V] -- Event outputs that no lane carries are silently host-only
These rows are not replayed (correctly, to avoid double spawns), but nothing else carries their output either:
- the rows the policy itself marks "no lane": arirShip, arirBuster, salt, rozitalHole, rockThrow, hillRoller, alienJump, trashBase (`event_fire_policy.cpp:47,67,71,84-85`);
- agrav, by design (`:58`).

`OnReliable` simply logs "NOT replayed" (`event_fire_sync.cpp:670-672`), so nothing appears on the client. This matches the bug report directly.

### RC2 [V] -- The EX_CallMath catch is gated by spawner class
`OnBeginDeferredExSpawn` returns before it even looks at the output class when the spawning object's class is not one of the 17 sources (`npc_world_enum.cpp:204-206`).

Consequences:
- An allowlisted world actor or NPC spawned from an unlisted Blueprint goes unmirrored with **no log**. Likely examples: `arirShip_C` from its trigger, `npc_arirFollower_C` from `trigger_spawnFollowingArir_C`, `skyUfo_C`, `skyFallingEvent_C`, droppers' cargo, children of `rozitBorg_C`/`morningUfo_C`.
- The PE interceptors (`world_actor_sync.cpp:243-247`) only see `ProcessEvent`-visible spawns, and a Blueprint graph's spawn is `EX_CallMath`, which they never see (docs/coop-dispatch-visibility.md:51-54).
- An output on neither allowlist is also dropped silently (`npc_world_enum.cpp:210`). There is no diagnostic that lists which classes the host spawned for an event but never mirrored.

### RC3 [V] mechanism, [?] visual impact -- The spawn-authority refusal blocks event visuals on mirrors
`spawn_authority` refuses the **whole** `ReceiveBeginPlay` of six world-actor event classes on any client session:
- `tentacleBallsFollower_C`, `superEgger_C`, `soltomiaCleaning_C`, `ufoDropper_C`/`_pig_C`/`_tank_C`, `firetankDeploy_C` (`coop/world/spawn_authority.cpp:106-112`);
- plus `kavotiaPatrolController_C` (`:115`).

`OnRowPre` (`:137-146`) cancels without checking whether the instance is a mirror, and there is no partial run.

Then the mirror receiver turns the actor tick off for every world-actor mirror (`world_actor_mirror.cpp:243`).

So whatever those classes set up in BeginPlay or Tick is gone on the client: the fly-in timeline, beam, lights, sound, material alpha, mesh attach. A mirror whose default state is hidden or alpha 0 renders as **nothing**. The docs admit the loss for creatures ("a ball's and a gray's mesh attach", docs/npcs-and-kerfur.md:70).

The status line "client-side shared-world spawners are refused at the script-body gate" (docs/status.md:84) is this table. It does not block the replay path itself, but it does strip the visuals of mirrored event actors.

### RC4 [?] -- Mirrors are dropped when the event class is not loaded on the client
- Both receivers resolve the class with `R::FindClass` (`world_actor_mirror.cpp:123-128`, `coop/creatures/npc_mirror.cpp:133-138`).
- `FindClass` is an object-array lookup only; it never loads (`ue_wrap/core/reflection.cpp:474-500`).
- The world-actor lane's own comment says event classes load lazily (`world_actor_sync.cpp:5-10`).
- Because the client never fires the event, a class that is not a hard import of a level actor may never be loaded on the client. The spawn is then dropped with "class '...' not loaded -- dropping", and there is no retry. Only a rejoin's snapshot gives it another chance.
- A loader already exists, `ue_wrap::asset_load::LoadObjectByPath` (`include/ue_wrap/core/asset_load.h:20`), but no mirror uses it.

### RC5 [V] -- Black fog has no host lane while the client suppresses its own
`weather_event_births.cpp:61-73` destroys every client `blackFog_C` birth. `MirrorEchoActive` never covers black fog (`:68-71`), and the host sends nothing. The client therefore never sees black fog.

### RC6 [?] -- Lane labels in the policy that are wrong or optimistic
- "graystank -> npc lane (grayTest_C)" (`event_fire_policy.cpp:67`): the menu says it is a tank+pig drop (`event_trigger.cpp:103`).
- vaccine, oil, begos and gascans are labelled "prop lane" (`event_fire_policy.cpp:81-82`), but their spawned classes look like event actors, not props.
- These rows are dropped on the client trusting a lane that may not exist.

### RC7 [V] -- Docs and code disagree, which hides the gaps
- docs/npcs-and-kerfur.md:70-71 says the client refuses the vent crawler, grays, eggs and balls gameplay bodies and the gray controller's mint. `spawn_authority.cpp:68-129` has no such rows; `npc_mirror_brain_table.cpp:46-52` parks only the zombies and the fossilhound.
- docs/events-and-weather.md:302 says the jellyfish catch does not see its spawn, but `jellyfishPath_C` is a source (`npc_world_enum.cpp:148`).

### Not a root cause (checked)
- **Host detection** is robust. The gate sees every route, a `ProcessEvent` call, `EX_Local*` and context calls alike (docs/coop-dispatch-visibility.md:14-21).
  - If the scheduler caller check (`event_fire_sync.cpp:370`, `callerFunction == settime`) misses because `settime` is an event stub whose caller frame is the saveSlot ubergraph, replay rows still broadcast through `:403-409`. The non-replay rows do not need a broadcast. Practically harmless [?].
  - Exception: a host fire made inside a mod-reflected dispatch returns at `:385` (`fromOurCode`).
- **Client refusal of the replay itself:** `NativeFire` admits exactly `{eventer, runEvent}` (`:315-320`, `:258-261`). If the admission mismatches, it logs `"was refused at the gate -- the admission did not match"` (`:329-333`) and the row is consumed for good, because `g_replayed` is set at `:490`. Grep the client log for that line.
- **Gate disabled:** if the runEvent watch "settled dead" (`:584-587`), HostFire is refused and nothing crosses. Grep for that line too.

## 4. Generic fallback: does one exist, and can one be added safely?

**What exists**
- A generic fallback for props only (`host_spawn_watcher.cpp:251-283`).
- A client-side "dissolve" pattern that nulls a Blueprint spawn's result and destroys the actor from a posted task, chosen by source or class (`coop/world/explosion_sync.cpp:165-197`, using `UH::CurrentResult()`).

**What does not exist**
- Mirroring of an unknown non-prop actor class.
- Any diagnostic for it.

**Can it be added?** Yes, in layers, but a blind "mirror everything" is not safe:
- A mirrored actor of unknown class runs its BeginPlay on the client (`world_actor_mirror.cpp:213-226`), which can spawn its own children, damage, `setEvent`, write save state, or send emails.
- The client trust gate (`world_actor_mirror.cpp:93-98`) exists to stop arbitrary class names.

The safe layering:
1. Diagnose: log which classes were left out.
2. Make the world-actor catch source-agnostic.
3. Make mirrors inert against child spawns instead of refusing their BeginPlay.
4. Curate classes from the logs.
5. Optionally, a host-advertised dynamic world-actor allowlist behind a config flag.

## 5. Proposed fixes, ranked by confidence and blast radius

**F1 -- Diagnostics** (zero risk, do first; it turns every [?] above into data)
- In `npc_world_enum.cpp:206-210`: when `source` matches but the output is not allowlisted, and `!IsDescendantOfProp`, log once per (source, class): `"EX-catch: host-only event output <class> from <source>"`.
- In `world_actor_mirror.cpp:125` and `npc_mirror.cpp:135`: keep the existing WARN lines; they are the RC4 evidence.
- In `event_fire_sync.cpp:670`: raise the log to WARN when `lane` contains "no lane".

**F2 -- Source-agnostic world-actor catch** (high confidence, low risk)
- In `OnBeginDeferredExSpawn`, test `IsWaAllowlistedClass(cls)` **before** the source gate, and enqueue world-actor outputs from any source.
- Keep the source gate for NPC outputs only. Per-peer spawners such as zombie hordes could otherwise double up.
- `HostEnrollExSpawn` already dedupes through `g_actorToWaId` (`world_actor_sync.cpp:675-680`).
- Fixes arirShip_C, skyUfo_C, skyFallingEvent_C and kocker_C wherever they are spawned.

**F3 -- Extend sources and allowlists from the F1 data** (medium-high confidence, low risk per class)
- Add to `kExSpawnSources` (`npc_world_enum.cpp:140-158`): `trigger_spawnFollowingArir_C` (arirFollower), the arirShip arm/trigger class, `ufoDropper_body_C`/`ufoDropper_car_C`/`ufoDropper_C` (cargo), `arirBusterSpawner_C`, `hillRollerSpawner_C`, `rozitBorg_C`, `morningUfo_C`.
- Add pose-only outputs to `kWorldActorAllowlist` (`sdk_profile_names.h:151-178`): `saltpile_C`, `event_bottomHoleController_C`, `rockThrower_C`, `alienJump_C`, `arirTrasher_C`, plus the buster creature class once F1 names it.
- Then fix the matching policy labels (`event_fire_policy.cpp:47,67,71,84-85`).
- Pair each new class with F4. Otherwise its client-side BeginPlay duplicates its children.

**F4 -- Replace whole-BeginPlay refusals with child-spawn dissolve on mirrors** (medium confidence, medium risk)
- Add a client post hook on `BeginDeferredActorSpawnFromClass`, cloned from `explosion_sync.cpp:165-197`. When `sourceObject` is a world-actor/NPC mirror (`world_actor_sync::NoteMirrorActor` set, or an NPC mirror reverse-map hit) and the call is outside a lane's materialize/apply scope, null the result and post the destroy.
- Then remove the `ReceiveBeginPlay` rows at `spawn_authority.cpp:106-112`. Mirrors keep their BeginPlay visuals (lights, beams, fades, sounds) but cannot mint duplicates.
- Risk: a purely cosmetic child actor gets dissolved too. Keep a small allow-list of cosmetic child classes.
- Re-evaluate `SetActorTickEnabled(false)` (`world_actor_mirror.cpp:243`) per class: classes whose fade or material animation is tick-driven need their tick, with the pose write still overriding the transform.

**F5 -- Class-load fallback on mirror receive** (high value if RC4 is confirmed, low risk)
- In `world_actor_mirror.cpp:123` and `npc_mirror.cpp:133`, on a `FindClass` miss, call `asset_load::LoadObjectByPath` using a static name -> `/Game/...` path table for the allowlisted classes.
- Alternatively, queue the spawn and retry each second for N seconds instead of dropping it.
- No protocol change is needed with the table approach.

**F6 -- Black fog lane** (medium confidence, medium risk)
- The host observes `blackFog_C` births at the same FinishSpawning seam (`weather_event_births.cpp:52`, currently client-only at `:53`) and sends its transform.
- The client spawns it inside an echo flag that `OnFinishSpawnPost` honours, mirroring `weather_fog::MirrorEchoActive`.

**F7 -- Generic dynamic world-actor fallback** (lower confidence, higher risk; optional, behind a flag such as `world.mirror_unknown_event_actors=0`)
- The host enrols any non-prop, non-Pawn output of a listed event source (`trigger_eventer_C` and the controllers) as a world actor.
- The client accepts class names only from a host-sent per-session list, and only if each is an `AActor` subclass that is not a Pawn or prop.
- Requires F4 so the mirror's BeginPlay cannot spawn gameplay. `setEvent` on such mirrors must also be refused, by extending the `creature_event_relay` mirror-bound refusal.

**F8 -- Docs** (no risk): fix docs/npcs-and-kerfur.md:70-71 and docs/events-and-weather.md:302 to match the code (RC7).

## 6. What to grep in the next two-player test log (multivoid.log, both peers)

| Peer | Pattern | Tells you |
|---|---|---|
| Host | `event_fire: broadcast` / `host OBSERVED scheduler fire` | the host detected the fire |
| Client | `NOT replayed (` | the row was dropped on policy; the lane text says why |
| Client | `not in the replay policy` | unknown row (verdict -1) |
| Client | `was refused at the gate` | admission mismatch: the replay was lost |
| Either | `runEvent watch settled dead` | the event wire is dead this session |
| Client | `world-actor[client OnSpawn]: class .* not loaded` / `npc-sync[client OnSpawn]: class .* not found` | RC4 |
| Client | `spawn_authority[...]: client-refuse` | RC3: the mirror's BeginPlay was refused |
| Host | `npc-sync[ex-spawn]: enrolled` / `world-actor[host]: tracked` | what actually got mirrored for the event |
