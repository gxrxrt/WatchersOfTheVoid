// coop/interactables/drone_call_intent.cpp -- see coop/interactables/drone_call_intent.h.

#include "coop/interactables/drone_call_intent.h"

#include "coop/element/intent_authority.h"
#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/props/active_drive.h"

#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/game_thread.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/devices/drone.h"
#include "coop/session/net_pump.h"
#include "coop/player/players_registry.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/devices/drone_console.h"
#include "ue_wrap/devices/drone.h"
#include "ue_wrap/facts/function_row.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <cstring>
#include <iterator>
#include <array>

namespace coop::drone_call_intent {
namespace {

namespace D  = ue_wrap::drone_console;
namespace GT = ue_wrap::game_thread;
namespace R  = ue_wrap::reflection;
namespace sg = ue_wrap::script_gate;

// The verb the console's buttons arrive on, watched on the console's own class by name, so the watch
// matches whichever class the running world loads under that name: a level class is never kept by
// pointer across worlds.
const wchar_t* const kConsoleClass = L"droneConsole_C";
const wchar_t* const kActionVerb   = L"actionOptionIndex";
constexpr int kTagDroneCall = 0x44524f4e;  // 'DRON'

// The action the console's own switch routes to its faces. Its other cases, 10 and 11, are the
// lid, which is already a synced door and must keep running locally.
constexpr uint8_t kActionUse = 4;

// A console press is an arm's-length interaction, so the sender's reach is the desk's rather than
// the pile grab's: the console is a small box a player stands at.
constexpr float kConsoleReachUU = 400.0f;

// One press is one call. The drone's own body ignores a press it cannot honour, so a burst costs
// nothing but the dispatch; the bound is here so a stalled client's backlog neither stalls a frame
// nor arrives refused.
constexpr float    kPressBurst     = 2.0f;
constexpr float    kPressPerSecond = 1.0f;
constexpr size_t   kMaxPending     = 4;
constexpr uint64_t kRefusalSayMs   = 10000;

// The drone's drop-sack verb. A client's drone is a mirror with no say over the sack: its local
// dropSack minted a sack the host never had, and the heartbeat re-offered the option, so a client
// could drop sack after sack. The client's body is refused; the host's drone drops the one sack.
const wchar_t* const kDroneClass = L"drone_C";
const wchar_t* const kDropVerb   = L"dropSack";
constexpr int kTagDropSack = 0x44534b53;  // 'DSKS'
constexpr float kDroneReachUU = 600.0f;

std::atomic<coop::net::Session*> g_session{nullptr};
bool g_watchInstalled = false;
bool g_watchLive = false;
bool g_dropWatchInstalled = false;
bool g_dropWatchLive = false;

uint64_t g_sent = 0, g_flown = 0, g_denied = 0;
bool g_waitSaid[coop::net::kMaxPeers] = {};  // a sender's wait for its body, said once a streak

struct Bucket {
    float    tokens = kPressBurst;
    uint64_t lastMs = 0;
    uint64_t nextSayMs = 0;
};
Bucket g_rate[coop::net::kMaxPeers];
struct Request { coop::net::DroneFlyIntentPayload payload; uint64_t at; };
std::deque<Request> g_pending[coop::net::kMaxPeers];
uint64_t g_epoch = 0, g_occupancy[coop::net::kMaxPeers]{};
uint32_t g_world = 0;
bool g_ticking = false;
void World() {
    auto world = ue_wrap::world_identity::Generation();
    if (g_world == world) return;
    ++g_epoch; g_world = world;
    for (auto& q : g_pending) q.clear();
    for (auto& b : g_rate) b = {};
}
bool Current(uint64_t epoch, uint64_t occupant, uint8_t slot) {
    auto* s = g_session.load();
    return epoch == g_epoch && occupant == g_occupancy[slot] &&
           g_world == ue_wrap::world_identity::Generation() && s && s->connected();
}

bool TakeToken(uint8_t slot) {
    Bucket& b = g_rate[slot];
    const uint64_t now = coop::active_drive::NowMs();
    if (b.lastMs != 0 && now > b.lastMs) {
        b.tokens += kPressPerSecond * static_cast<float>(now - b.lastMs) / 1000.0f;
        if (b.tokens > kPressBurst) b.tokens = kPressBurst;
    }
    b.lastMs = now;
    if (b.tokens < 1.0f) return false;
    b.tokens -= 1.0f;
    return true;
}

// ---- host side -------------------------------------------------------------------------------
// One press, run or refused and then consumed, or left at the head of its queue: false while the host
// has no body for the sender, the seconds between a joiner's curtain and its first pose. That is a
// mid-join window rather than a verdict, which the door-verb, keypad and container lanes wait out the
// same way (principle 8): a press made in it is not lost.
bool Execute(coop::net::Session& s, const coop::net::DroneFlyIntentPayload& p, uint8_t slot) {
    const auto epoch = g_epoch, occupant = g_occupancy[slot];
    // The console carries no identity to name, so the sender's reach IS the resolve: of this
    // world's consoles, the one the sender is standing at. Any of them calls the same drone, so
    // the first within reach is the press.
    const bool dropSack = p.verb == coop::net::drone_verb::kDropSack;
    const auto token = coop::element::IntentTarget::ForClientIntent(
        s, slot, dropSack ? kDroneReachUU : kConsoleReachUU);
    if (!Current(epoch, occupant, slot)) return true;
    if (!token.HasBody()) {
        if (!g_waitSaid[slot]) {
            g_waitSaid[slot] = true;
            UE_LOGI("[DRONE-CALL] slot %u's presses wait: the host has no body for it yet",
                    static_cast<unsigned>(slot));
        }
        return false;
    }
    if (dropSack) {
        ue_wrap::CachedObjRef drone; drone.Set(ue_wrap::drone::Find());
        if (!drone.Get()) return true;
        const bool near = token.Authorize(drone.Raw()).outcome == coop::element::IntentOutcome::Ok;
        if (!Current(epoch, occupant, slot) || !drone.Get()) return true;
        if (!near || !(ue_wrap::drone::ReadFxBits(drone.Raw()) & ue_wrap::drone::kFxHasSack)) {
            ++g_denied;
            UE_LOGI("[DRONE-CALL] DENY drop-sack slot=%u -- %s", static_cast<unsigned>(slot),
                    near ? "the host's drone carries no sack" : "the drone is out of reach");
            return true;
        }
        const bool dropped = ue_wrap::drone::DropSack(drone.Raw());
        UE_LOGI("[DRONE-CALL] drop-sack for slot=%u %s", static_cast<unsigned>(slot),
                dropped ? "run on the host's drone" : "did not dispatch");
        return true;
    }
    void* consoles[4];
    const int32_t nc = D::LiveConsoles(consoles, static_cast<int32_t>(std::size(consoles)));
    std::array<ue_wrap::CachedObjRef, 4> candidates;
    for (int32_t i = 0; i < nc; ++i) candidates[i].Set(consoles[i]);
    void* console = nullptr;
    for (int32_t i = 0; i < nc && !console; ++i) {
        const auto ref = candidates[i];
        void* candidate = ref.Get();
        if (!candidate) continue;
        const bool ok = token.Authorize(candidate).outcome == coop::element::IntentOutcome::Ok;
        if (!Current(epoch, occupant, slot)) return true;
        if (ok) console = ref.Get();
    }
    if (!console) {
        ++g_denied;
        UE_LOGI("[DRONE-CALL] DENY slot=%u -- none of the %d live console(s) is within reach",
                static_cast<unsigned>(slot), nc);
        return true;
    }
    if (p.verb == coop::net::drone_verb::kLeaveTimer) {
        ue_wrap::CachedObjRef drone,target;drone.Set(ue_wrap::drone::Find());target.Set(console);
        bool leave=false;
        if(!drone.Get() || !ue_wrap::drone::ReadDeparturePreference(drone.Raw(),leave))return false;
        if(!Current(epoch,occupant,slot) || !drone.Get() || !target.Get())return true;
        const bool written=ue_wrap::drone::WriteDeparturePreference(drone.Raw(),!leave);
        if(!Current(epoch,occupant,slot) || !target.Get())return true;
        if(!written)return false;
        D::PlayChangeCue(target.Raw());
        return true;
    }
    if (p.verb == 1) {
        // The native timer face is usable with the lid closed. It changes only this field;
        // the host's existing timer/flight graph owns when the drone actually departs.
        return ue_wrap::drone::WriteDeparturePreference(ue_wrap::drone::Find(), p.leaveAfter5min != 0);
    }
    // The lid is the console's own gate on its keyboard, and it is shared state the box lane
    // (door_box) already carries, so the host reads its own copy rather than trusting the press.
    if (!D::IsLidOpen(console)) {
        ++g_denied;
        UE_LOGI("[DRONE-CALL] DENY slot=%u -- the console's lid is shut here",
                static_cast<unsigned>(slot));
        return true;
    }
    const bool flew = D::TriggerFly(console);
    if (!Current(epoch, occupant, slot)) return true;
    if (!flew) {
        ++g_denied;
        UE_LOGW("[DRONE-CALL] slot=%u -- triggerFly did not dispatch (no drone reference or the "
                "verb did not resolve)", static_cast<unsigned>(slot));
        return true;
    }
    ++g_flown;
    UE_LOGI("[DRONE-CALL] PRESSED console %p for slot=%u (#%llu) -- the drone's own body owns the "
            "outcome", console, static_cast<unsigned>(slot),
            static_cast<unsigned long long>(g_flown));
    return true;
}

// ---- the client's gate -----------------------------------------------------------------------
sg::Verdict OnActionPre(const sg::Call& call) {
    auto* s = g_session.load(std::memory_order_acquire);
    if (!s || !s->connected() || s->role() != coop::net::Role::Client || !call.object) return sg::Verdict::Run;

    if (!coop::net_pump::IsInAnnouncedWorld(call.object)) return sg::Verdict::Run;
    static R::ParamOffset action{L"action"}, player{L"player"};
    const int sActionOff = action.Of(call.function), playerOff = player.Of(call.function);
    if (!call.locals || sActionOff < 0 || playerOff < 0) return sg::Verdict::Cancel;
    void* who = nullptr; std::memcpy(&who, call.locals + playerOff, sizeof(who));
    if (who != coop::players::Registry::Get().Local()) return sg::Verdict::Cancel;
    if (*reinterpret_cast<uint8_t*>(call.locals + sActionOff) != kActionUse)
        return sg::Verdict::Run;  // the lid's cases stay the game's, on every peer

    // Native priority: timer face before keyboard. Do not replay a local timer toggle.
    const bool departure = D::IsCursorOnDeparture(call.object);
    if (!departure && !D::IsCursorOnKeyboard(call.object)) return sg::Verdict::Run;

    coop::net::DroneFlyIntentPayload p{};
    p.verb = departure ? coop::net::drone_verb::kLeaveTimer : coop::net::drone_verb::kCall;
    World();
    if (g_pending[0].size() < kMaxPending) g_pending[0].push_back({p, coop::active_drive::NowMs()});
    else {
        UE_LOGW("[DRONE-CALL] console request queue full; the drone keeps the host's state");
        return sg::Verdict::Cancel;
    }
    return sg::Verdict::Cancel;
}

sg::Verdict OnDropSackPre(const sg::Call& call) {
    auto* s = g_session.load(std::memory_order_acquire);
    if (!s || !s->connected() || s->role() != coop::net::Role::Client || !call.object || call.fromOurCode)
        return sg::Verdict::Run;
    if (!coop::net_pump::IsInAnnouncedWorld(call.object)) return sg::Verdict::Run;
    // Only the player's own option press becomes a request; any other route on the mirror (a timer
    // the suppressed tick left behind) is simply refused, since the host's drone owns the sack.
    const bool pressed = call.callerFunction &&
        R::NameEquals(R::NameOf(call.callerFunction), L"actionOptionIndex");
    if (!pressed) return sg::Verdict::Cancel;
    coop::net::DroneFlyIntentPayload p{};
    p.verb = coop::net::drone_verb::kDropSack;
    World();
    if (g_pending[0].size() < kMaxPending) {
        g_pending[0].push_back({p, coop::active_drive::NowMs()});
        UE_LOGI("[DRONE-CALL] drop-sack refused on this mirror -- asking the host's drone");
    }
    return sg::Verdict::Cancel;
}

// ---- the host's stall backstop ----------------------------------------------------------------
// Reported in play: a drone that never flies back. Two shapes are seen from the host's own state,
// so the backstop needs no packet. Parked at the drop with the five-minute departure chosen and
// still there well past it (a sack lane left the host's drone without its sack, so triggerFly keeps
// refusing): press the console's verb, and on a second try give the drone its sack flag back first.
// Flying but not moving for a long time (blocked short of its pad): lift it. "Wait indefinitely"
// is the player's choice and is never forced. Only an Active drone is judged: a dormant one at home
// is left alone, so a wrong read fails safe.
constexpr uint64_t kParkedOverdueMs = 390000;  // the five-minute departure plus a grace
constexpr uint64_t kStuckMs         = 45000;
constexpr uint64_t kRetryMs         = 5000;
constexpr float    kMovedUU         = 50.0f;
constexpr int      kMaxNudges       = 3;
struct Backstop {
    uint64_t nextCheckMs = 0, parkedSinceMs = 0, lastMoveMs = 0, nextActMs = 0;
    ue_wrap::FVector lastPos{};
    bool havePos = false;
    int presses = 0, nudges = 0;
};
Backstop g_wd;

void HostBackstop() {
    const uint64_t now = coop::active_drive::NowMs();
    if (now < g_wd.nextCheckMs) return;
    g_wd.nextCheckMs = now + 250;
    void* drone = ue_wrap::drone::Find();
    ue_wrap::FVector loc; ue_wrap::FRotator rot;
    if (!drone || !ue_wrap::drone::IsActive(drone) || !ue_wrap::drone::ReadPose(drone, loc, rot)) {
        g_wd = Backstop{}; g_wd.nextCheckMs = now + 1000;
        return;
    }
    const uint8_t bits = ue_wrap::drone::ReadFxBits(drone);
    const bool arrived = (bits & ue_wrap::drone::kFxArrived) != 0;
    const bool hasSack = (bits & ue_wrap::drone::kFxHasSack) != 0;
    const bool leave5  = (bits & ue_wrap::drone::kFxLeaveAfter5min) != 0;
    const float dx = loc.X - g_wd.lastPos.X, dy = loc.Y - g_wd.lastPos.Y, dz = loc.Z - g_wd.lastPos.Z;
    if (!g_wd.havePos || dx * dx + dy * dy + dz * dz > kMovedUU * kMovedUU) {
        g_wd.lastPos = loc; g_wd.havePos = true; g_wd.lastMoveMs = now; g_wd.nudges = 0;
    }
    if (!arrived) { g_wd.parkedSinceMs = 0; g_wd.presses = 0; }
    else if (!g_wd.parkedSinceMs) g_wd.parkedSinceMs = now;
    if (now < g_wd.nextActMs) return;

    if (arrived && leave5 && now - g_wd.parkedSinceMs > kParkedOverdueMs && g_wd.presses < 2) {
        void* consoles[4];
        if (D::LiveConsoles(consoles, 4) < 1) return;
        // triggerFly refuses while the drone has no sack and one lies in the world; the second try
        // hands the flag back so the departure is not held hostage by a stray sack.
        if (g_wd.presses == 1 && !hasSack) ue_wrap::drone::WriteGateFields(drone, true, true);
        const bool pressed = D::TriggerFly(consoles[0]);
        ++g_wd.presses; g_wd.nextActMs = now + kRetryMs;
        UE_LOGW("[DRONE-WD] parked %llus past arrival (sack=%d) -- departure press %d %s",
                static_cast<unsigned long long>((now - g_wd.parkedSinceMs) / 1000), hasSack ? 1 : 0,
                g_wd.presses, pressed ? "dispatched" : "did not dispatch");
        return;
    }
    if (!arrived && now - g_wd.lastMoveMs > kStuckMs && g_wd.nudges < kMaxNudges) {
        const bool moved = ue_wrap::drone::Nudge(drone, ue_wrap::FVector{0.f, 0.f, 300.f});
        ++g_wd.nudges; g_wd.lastMoveMs = now; g_wd.nextActMs = now + kRetryMs;
        UE_LOGW("[DRONE-WD] in flight but still for %llus -- lift %d/%d %s",
                static_cast<unsigned long long>(kStuckMs / 1000), g_wd.nudges, kMaxNudges,
                moved ? "applied" : "failed");
    }
}

void InstallWatches() {
    if (!g_watchInstalled)
        g_watchInstalled = sg::WatchClassName(kConsoleClass, kActionVerb, kTagDroneCall, &OnActionPre, nullptr);
    if (!g_dropWatchInstalled)
        g_dropWatchInstalled = sg::WatchClassName(kDroneClass, kDropVerb, kTagDropSack, &OnDropSackPre, nullptr);
}

}  // namespace

void Install(coop::net::Session* session) {
    g_session.store(session, std::memory_order_release);
    World();
    InstallWatches();
}

void Tick(coop::net::Session& session) {
    sg::ResolvePendingNames();
    if (!g_watchInstalled || !g_dropWatchInstalled) {
        InstallWatches();
        return;
    }
    if (!g_watchLive && sg::ClassNameWatchLive(kConsoleClass, kActionVerb, kTagDroneCall)) {
        g_watchLive = true;
        UE_LOGI("[DRONE-CALL] the console action gate is live");
    }
    if (!g_dropWatchLive && sg::ClassNameWatchLive(kDroneClass, kDropVerb, kTagDropSack)) {
        g_dropWatchLive = true;
        UE_LOGI("[DRONE-CALL] the drop-sack gate is live");
    }
    if (!session.connected() || g_ticking) return;
    World();
    struct Guard { Guard(){g_ticking=true;} ~Guard(){g_ticking=false;} } guard;
    if (session.role() != coop::net::Role::Host) {
        auto& q = g_pending[0];
        if (q.empty()) return;
        const auto r = q.front();
        if (coop::active_drive::NowMs() - r.at > 3000) { q.pop_front(); return; }
        if (session.SendReliable(coop::net::ReliableKind::DroneFlyIntent, &r.payload, sizeof(r.payload))) {
            q.pop_front();
            if (r.payload.verb == 0) ++g_sent;
        }
        return;
    }
    HostBackstop();
    for (uint8_t slot = 1; slot < coop::net::kMaxPeers; ++slot) {
        if (g_pending[slot].empty() || !TakeToken(slot)) continue;
        const Request r = g_pending[slot].front();
        g_pending[slot].pop_front(); // never retain a deque reference across native callbacks
        if (coop::active_drive::NowMs() - r.at > 3000) continue;
        const auto epoch = g_epoch, occupant = g_occupancy[slot];
        const bool consumed = Execute(session, r.payload, slot);
        if (!Current(epoch, occupant, slot)) return;
        if (consumed) {
            g_waitSaid[slot] = false;        // a consumed press ends the wait's streak
        } else {
            g_pending[slot].push_front(r);
            if (g_pending[slot].size() > kMaxPending) g_pending[slot].pop_back();
            g_rate[slot].tokens += 1.0f;     // a wait runs nothing, so it spends no token
        }
    }
}

void OnDroneFlyIntent(coop::net::Session& session, const coop::net::DroneFlyIntentPayload& payload,
                      uint8_t senderSlot) {
    if (session.role() != coop::net::Role::Host || !session.connected() || payload.verb > coop::net::drone_verb::kMax ||
        (payload.verb == 1 && payload.leaveAfter5min > 1)) return;
    World();
    if (senderSlot < 1 || senderSlot >= coop::net::kMaxPeers) return;
    auto& q = g_pending[senderSlot];
    if (q.size() >= kMaxPending) {
        Bucket& b = g_rate[senderSlot];
        const uint64_t now = coop::active_drive::NowMs();
        if (now >= b.nextSayMs) {
            b.nextSayMs = now + kRefusalSayMs;
            UE_LOGW("[DRONE-CALL] slot %u queue full (%zu) -- refusing presses until it drains",
                    static_cast<unsigned>(senderSlot), q.size());
        }
        ++g_denied;
        return;
    }
    q.push_back({payload, coop::active_drive::NowMs()});
}

void OnPeerLeft(uint8_t slot) {
    if (slot >= coop::net::kMaxPeers) return;
    ++g_occupancy[slot];
    g_pending[slot].clear();
    g_rate[slot] = Bucket{};
    g_waitSaid[slot] = false;
}

uint64_t SentCount() { return g_sent; }

void OnDisconnect() {
    ++g_epoch; g_session.store(nullptr);
    if (g_sent || g_flown || g_denied)
        UE_LOGI("[DRONE-CALL] session end -- sent=%llu pressed=%llu denied=%llu",
                static_cast<unsigned long long>(g_sent), static_cast<unsigned long long>(g_flown),
                static_cast<unsigned long long>(g_denied));
    for (uint8_t slot = 0; slot < coop::net::kMaxPeers; ++slot) {
        g_pending[slot].clear();
        g_rate[slot] = Bucket{};
        g_waitSaid[slot] = false;
    }
    g_sent = g_flown = g_denied = 0;
    g_wd = Backstop{};
}

}  // namespace coop::drone_call_intent
