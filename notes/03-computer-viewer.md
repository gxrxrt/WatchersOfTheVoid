# Bug #3 -- in-game computer stuck on the "Viewer" tab (hash codes scrolling), tabs won't switch

Scope: read-only investigation of WatchersOfTheVoid-main (source protocol ~2159/2160; installed package 0.9.2160).
Paths below are relative to `src/votv-coop/` unless they start with `docs/` or `agent/`.
[V] = verified by reading the code. [?] = hypothesis / needs a runtime check.

## 1. Which computer this is

- [V] The only in-game computer with tabs is the base laptop (`laptop_C`) and the portable PCs
  (`prop_portablePc_C`). They all render one widget, `ui_laptop_C`, and share one occupancy claim key
  `"laptop"` (`src/ue_wrap/desk/device_screen.cpp:47-71`). The signal desk (`ui_consolesAtlas_C`, claim
  "desk") has no tab switcher. Its lanes (`desk_visual_sync`, `desk_input_sync`, `console_state_sync`) carry
  aim, cursor and scalars, not pages.
- [V] `ui_laptop_C` contains a `rootswitch` (0 = boot, 1 = desktop), the main tab switcher `switcher`, and two
  inner switchers `switcher_media` and `switcher_mediaViewer` (`src/ue_wrap/devices/laptop_ui.cpp:16`,
  `src/ue_wrap/desk/shared_screen.cpp:71-81`).
- [?] The "Viewer" tab is most likely the laptop's media-viewer page (`switcher_mediaViewer`, called
  "media-viewer page" in `docs/devices.md:351-353`), or a main-switcher page that has a constantly animating
  list or scroll box (the moving hash codes). Either way, every candidate mechanism below works on the same
  widget, and all of them can pin the main `switcher`.

## 2. Three lanes write the same laptop switchers

| Lane | What it writes | When it writes | Code |
|---|---|---|---|
| `laptop_tab` | `switcher` (main) via `SetActiveWidgetIndex` | event-driven: the holder's `switcherSwitched` POST, then host check, then fan-out. A viewer writes once per state message. | `src/coop/interactables/laptop_tab.cpp:142-164, 200-218, 302-329` |
| `laptop_ui_sync` | `switcher`, `switcher_media`, `switcher_mediaViewer` and the cart | 150 ms poll. It re-applies the canonical **every poll while `g_apply` is true**. | `src/coop/interactables/laptop_ui_sync.cpp:112-156`, `src/ue_wrap/devices/laptop_ui.cpp:100-169` |
| `shared_screen_sync` | the **whole desktop subtree**: every visibility, every WidgetSwitcher index, every ScrollBox offset and every TextBlock text | 200 ms poll. It re-applies **every poll while `g_apply` is true, OR whenever this peer is not the Author** | `src/coop/interactables/shared_screen_sync.cpp:105-140`, `src/ue_wrap/desk/shared_screen.cpp:41-138` |

[V] The script gate does not block a tab switch. The only `ui_laptop_C` tab watch is POST-only with no
pre-handler (`laptop_tab.cpp:169`, `WatchName(row, tag, nullptr, &OnPost)`). The other `ui_laptop_C` rows
are order, cart and database verbs (`order_sync.cpp:199-200`, `meadow_db_writers.cpp:15-17`,
`camera_toggle_intent.cpp:45`). None of them is a tab verb.

## 3. Ranked hypotheses

### H1 (high confidence). `shared_screen_sync` latches `g_apply` and repaints a stale snapshot every 200 ms; the local operator never regains control

Mechanism, all [V]:
1. Every time the laptop claim changes, the host starts a new lease and sets `g_apply = !g_canonical.empty()`
   (`shared_screen_sync.cpp:60-68`). The claim revision is bumped on every host `SendClaim("laptop")`
   (`device_occupancy.cpp:194`). A client receiving a canonical with a new lease, or while it is not the
   Author, sets `g_target = rows; g_apply = true` (`shared_screen_sync.cpp:162-167`). The host also sets
   `g_apply = true` on every accepted client snapshot (`:160`).
2. Each tick: `if (!g_target.empty() && (g_apply || !Author())) { applied = UI::Apply(target); if (applied)
   g_apply = false; }` (`:114-122`). **The only way to clear `g_apply` is a fully successful Apply.** There is
   no retry cap and no timeout.
3. While `g_apply` is true the operator's own capture is suppressed: `if ((!g_apply && Author()) || ...)
   Capture();` (`:123-126`). So the operator never publishes, and the host's canonical never changes.
4. `UI::Apply` is all-or-nothing. Any row whose path is missing locally sets `ok = false`
   (`shared_screen.cpp:115-116`). So does a tab index outside the local child count (`:122-127`), a failed
   SetVisibility (`:120-121`), or a failed SetText (`:130-135`). A failing Apply still **performs every write
   it can**, including setting the main `switcher` to the snapshot's tab (`:122-125`).
5. Row paths are child-index strings built by `Walk`. Walk descends only into the active child of a
   switcher, stops at Collapsed/Hidden widgets, and stops at 512 entries
   (`shared_screen.cpp:41-67`, cap at `:42`). Any page whose child count differs between peers produces
   paths that never exist on the other peer. Examples: lists generated at runtime (mail rows, store and cart
   rows, file and database rows, or a list of hash-code lines appended by a timer), or a tree past the
   512-entry cap. With such a page Apply can **never** succeed, so `g_apply` stays true for good.

Result: the operator's widget is forced back to the snapshot's page (the Viewer) every 200 ms, so every tab
click is reverted almost at once. That is exactly "stuck on the Viewer tab, won't switch". It persists:
- On a client, `g_apply` is cleared only by a successful Apply or a world change (`Clear`, `:34-47`).
  Leaving the PC makes the client a non-Author, which keeps repainting (`:114`). The host publishes nothing
  new, because it captures only as Author or when its canonical is empty (`:123`). So the client stays pinned
  until world reload or disconnect.
- On the host, re-entering starts a new lease and sets `g_apply = true` (`:67`). If its own old snapshot
  (taken on the animated Viewer page) no longer matches the tree, the host gets pinned the same way.
- A peer that has just sat down but has no verdict yet is `!Author()`. It is repainted every 200 ms during
  the round trip, so early clicks are lost even in the good path (`:69-72, :114`).

Supporting evidence:
- [V] This lane already caused the same kind of bug once. b2179 "PC stays loading": an old boot snapshot
  repainted `rootswitch` to 0 every poll (`agent/TESTLOG.md:1402-1409`). The fix only removed rootswitch
  from the walk (`shared_screen.cpp:73-81`). The same repaint loop still covers every switcher inside the
  desktop subtree.
- [V] The unit test codifies "a newly local operator must inherit the host's complete screen before
  publishing". The operator stays mute while Apply fails (`agent/tests/shared_screen_sync_test.cpp:101-106`).
  No test covers an Apply that fails permanently.
- [V] Nothing logs this state. `shared_screen_sync.cpp` has no log line at all, so the stuck state is
  invisible in multivoid.log.
- [?] Animated text and scroll (the moving hash codes) make the host operator publish a new canonical every
  200 ms (`Capture`, `:88-90`, compares whole byte blobs). Each snapshot can be up to 128 KB (`shared_screen_wire.h:8`)
  in 218-byte chunks (`protocol.h:2689-2696`), so this can also flood the reliable lane. Each viewer SetText
  goes through `MintFText`, which deliberately leaks one FText reference per call (`src/ue_wrap/core/ftext_utils.cpp:106-109`).
- [?] Mirrored `SetText` on a TextBlock unbinds its UMG text binding (in UE 4.27, `UTextBlock::SetText` calls
  `TextDelegate.Unbind()`). Mirrored `SetVisibility` overrides bound visibility. Inherited Collapsed tab
  buttons or panels can then stay hidden on the new operator, because its Blueprint state never ran the code
  that hid them.

### H2 (medium-high). `laptop_ui_sync` has the same latch on the three switchers (150 ms)

- [V] `if (g_apply) { applied = UI::Apply(state); ... if (applied) g_apply = false; }`
  (`laptop_ui_sync.cpp:120-127`). The author read is suppressed while `g_apply` is set: `author = !g_apply && ...`
  (`:130`). `g_apply` is set on every lease change (`:56`), on a peer leaving (`:185`), and on a client
  receiving a new lease or a non-local canonical (`:100-105`).
- [V] `UI::Apply` writes all three switchers, including `switcher_mediaViewer`, **before** it checks the
  result (`laptop_ui.cpp:162-166`). It then returns `Read(actual) && actual == s` (`:167-168`). So a mismatch
  in the **cart** or a failed `Read` makes the tab writes repeat every 150 ms. `Read` fails when:
  - the cart holds an unnamed row (`requireNames=true`, `laptop_ui.cpp:58, 89`);
  - the catalog is not ready or does not contain a row (`Valid`, `:70-80`);
  - a switcher index is outside fixed bounds (`tabs[0] <= 11`, `tabs[1..2] <= 1`, `:68-69`).
  The cart can also come out different after the native `addStoreCart`/`removeStoreCart`/`genStoreCart`, for
  example from a capacity or upgrade-level difference (`:133-160`). Any of these keeps `g_apply` true and
  pins main tab plus media-viewer page. The match to the word "Viewer" makes this a strong co-suspect.
- [V] There is no log line here either.

### H3 (medium). Several writers disagree on the switcher

- [V] A viewer receives the main tab from three sources at different times: `laptop_tab` (immediate),
  `laptop_ui_sync` (150 ms) and `shared_screen_sync` (200 ms, plus a repaint every tick while not Author).
  If both latches (H1 and H2) are active with different snapshots (`laptop_ui` `g_state` and `shared_screen`
  `g_target` are captured at different moments, `laptop_ui_sync.cpp:137` and `shared_screen_sync.cpp:89`),
  the page flips between them. Either way the local click loses.

### H4 (low). Occupancy

- [V] One `"laptop"` claim covers the base PC and every portable PC (`device_screen.cpp:62-71`). A peer that
  is denied is force-exited (`device_occupancy.cpp:400-410, 515-519`). That shows as "kicked out of the
  computer", not as "stuck on a tab". It matters only because it makes `Author()` false, which feeds H1.

### H5 (low). ImGui or overlay input capture

- [V] With `ui_laptop_C` open, `input_owner` reports that the game owns input (`gameOwnsText -> YES
  (owner=ui_laptop_C ...)` in the current multivoid.log, from `src/coop/input/input_owner.cpp:376`). Tab clicks
  are in-world cursor clicks, not keys the overlay swallows. Unlikely.

## 4. Proposed fixes, ranked by confidence and blast radius

### Fix 1 (hotfix: high confidence, small blast radius). Never repaint the local operator

`shared_screen_sync.cpp:114`:
```cpp
const bool localOperator = device_occupancy::LocalHolds(L"laptop");
if (localOperator && g_apply && ++g_applyTries > 2) { g_apply = false; g_applyTries = 0; }  // bounded inherit
if (!g_target.empty() && (localOperator ? g_apply : (g_apply || !Author()))) { ... }
```
- Reset `g_applyTries = 0` wherever `g_apply` is set (`:67, :160, :166`, and in `Clear`).
- Effect: a new operator tries to inherit the previous screen at most about 3 times (0.6 s), then owns its
  widget. A peer that holds the claim locally but has no verdict yet is not repainted. Observers keep
  mirroring.
- Do the same in `laptop_ui_sync.cpp:120-127`: cap the attempts when `device_occupancy::LocalHolds(L"laptop")`,
  and never let `g_apply` block `author` (`:130`) for longer than the cap.
- Cost: the operator may sometimes not inherit the previous page, scroll or cart. These are cosmetic; the
  real economy goes through `order_sync`.

### Fix 2 (high confidence, medium radius). Make Apply converge instead of all-or-nothing

- `shared_screen.cpp:115-116`: a missing path should not fail the apply. Count it and skip it. Return
  failure only for calls that really failed, and only for the first two passes after a switcher or
  visibility change (the "late-child retry" the b2179 test relies on). After that, give up.
- `laptop_ui.cpp:162-168`: judge the tabs on their own. Return "tabs applied" once
  `GetActiveWidgetIndex == s.tabs[i]`, and treat a cart mismatch as a separate, bounded retry that never
  rewrites the switchers.
- Add one `UE_LOGW` the first time Apply fails N times in a row, naming the first missing path or the failed
  verb. Today the latch is silent, and this log line would confirm H1 or H2 in the field.

### Fix 3 (medium confidence, medium radius). One owner per switcher

- `laptop_tab` is the reviewed and drilled owner of `switcher` (`docs/base-computer.md:36-42`). In
  `shared_screen.cpp` ReadRow/Apply (`:86, :122`), skip the WidgetSwitcher flag for widgets whose FName is
  `switcher`, `switcher_media` or `switcher_mediaViewer`. Those already ride `laptop_tab` and
  `laptop_ui_sync`.
- Optionally drop `tabs[0]` from `laptop_ui_sync` and keep only the media switchers and the cart.

### Fix 4 (longer term). Do not mirror animated presentation

- Exclude ScrollBox offsets and TextBlock text under pages that animate (the hash-code viewer), or
  rate-limit host publishes to "on a tab, visibility or user input change". This removes the 5 Hz,
  up-to-128 KB reliable traffic and the `MintFText` leak, and avoids unbinding UMG text bindings.

## 5. How to confirm in game

1. Host and client. The host opens the laptop, goes to the Viewer tab, and leaves. The client opens the
   laptop and tries to switch tabs. Expected with the current code: the client is pinned. Repeat with the
   roles reversed, and with the host re-entering after the client.
2. Add temporary logging at `shared_screen_sync.cpp:119-121` and `laptop_ui_sync.cpp:123-126`: log
   `applied`, `g_apply`, `Author()`, and the first missing path from `shared_screen.cpp:116`. A pinned peer
   will show `applied=0` repeating every 200 ms (H1) or 150 ms (H2).
3. Quick A/B test with no design change: comment out the `shared_screen_sync::Tick()` call
   (`src/coop/session/subsystems.cpp:1009`). If the tabs work again, H1 is confirmed. Do the same with
   `laptop_ui_sync::Tick()` (`subsystems.cpp:923`) to test H2.
