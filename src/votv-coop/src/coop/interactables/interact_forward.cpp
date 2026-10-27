// coop/interactables/interact_forward.cpp -- see coop/interactables/interact_forward.h.

#include "coop/interactables/interact_forward.h"

#include "coop/element/intent_authority.h"
#include "coop/element/portable_identity.h"
#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/net/wire_key_util.h"
#include "coop/player/players_registry.h"
#include "coop/player/remote_player.h"
#include "coop/props/prop_save_data.h"
#include "coop/session/net_pump.h"

#include "ue_wrap/actors/hook.h"       // ComponentByName
#include "ue_wrap/actors/prop.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/reflection_props.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/engine/hit_result.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace coop::interact_forward {
namespace {

namespace R  = ue_wrap::reflection;
namespace sg = ue_wrap::script_gate;
namespace V  = coop::net::interact_verb;

enum class Policy : uint8_t { None, Everywhere, HostOnly };
struct ClassRow { const wchar_t* name; Policy policy; };
constexpr ClassRow kClasses[] = {
#include "coop/interactables/interact_forward_classes.inc"
};

constexpr int kTagBase = 0x49460000;  // 'IF', then the verb
constexpr const wchar_t* kVerbs[] = {L"actionOptionIndex", L"playerUsedOn", L"player_use"};
constexpr float kReachUU = 600.0f;
constexpr int kPressesPerSecond = 8;

coop::net::Session* g_session = nullptr;
bool g_installed = false;

uint64_t NowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

// The class's policy, cached per class object. Exact classes only: a subclass the census did not list
// may be a dedicated lane's.
Policy PolicyOf(void* object) {
    static std::unordered_map<void*, Policy> s_cache;
    void* cls = object ? R::ClassOf(object) : nullptr;
    if (!cls) return Policy::None;
    if (auto it = s_cache.find(cls); it != s_cache.end()) return it->second;
    const std::wstring name = R::ClassNameOf(object);
    Policy p = Policy::None;
    for (const ClassRow& row : kClasses)
        if (name == row.name) { p = row.policy; break; }
    if (s_cache.size() < 8192) s_cache.emplace(cls, p);
    return p;
}

// The 1-byte look-at fields a class carries (lookingAt_*, lookAt*), own and inherited: what the
// presser's trace wrote and the receiver's copy does not know. Cached per class.
const std::vector<int32_t>& LookFields(void* cls) {
    static std::unordered_map<void*, std::vector<int32_t>> s_fields;
    if (auto it = s_fields.find(cls); it != s_fields.end()) return it->second;
    std::vector<int32_t> out;
    for (void* c = cls; c && out.size() < coop::net::kInteractFlags; c = R::SuperStructOf(c)) {
        for (const auto& f : R::EnumerateStructFields(c)) {
            if (f.size != 1 || f.offset < 0 || f.offset > 0xFFFF) continue;
            if (f.name.rfind(L"lookingAt", 0) != 0 && f.name.rfind(L"lookAt", 0) != 0) continue;
            out.push_back(f.offset);
            if (out.size() >= coop::net::kInteractFlags) break;
        }
    }
    return s_fields.emplace(cls, std::move(out)).first->second;
}

template <class T> bool ReadParam(const sg::Call& c, const wchar_t* name, T& out) {
    const int off = R::FindParamOffset(c.function, name);
    if (!c.locals || off < 0) return false;
    std::memcpy(&out, c.locals + off, sizeof(T));
    return true;
}

void* PlayerFor(uint8_t slot) {
    auto& reg = coop::players::Registry::Get();
    if (slot == reg.LocalPeerId()) return reg.Local();
    auto* puppet = reg.Puppet(slot);
    return puppet ? puppet->GetActor() : nullptr;
}

// The target on this peer: the live instance of the class whose portable identity matches.
void* Resolve(const coop::net::InteractForwardPayload& p) {
    char cls[sizeof(p.className.data) + 1] = {};
    std::memcpy(cls, p.className.data, std::min<size_t>(p.className.len, sizeof(p.className.data)));
    const std::wstring wcls(cls, cls + std::strlen(cls));
    const std::wstring key = coop::net::StringFromWireKey(p.key);
    void* classObj = ue_wrap::object_index::ClassByName(wcls.c_str());
    if (!classObj || key.empty()) return nullptr;
    struct Ctx { const std::wstring* key; void* found; } ctx{&key, nullptr};
    ue_wrap::object_index::ForEachInstance(classObj, [](void* c, void* obj, int32_t) {
        auto* x = static_cast<Ctx*>(c);
        if (x->found || !obj || !R::IsLive(obj) || !coop::net_pump::IsInAnnouncedWorld(obj)) return;
        if (coop::element::PortableWireKey(obj) == *x->key) x->found = obj;
    }, &ctx);
    return ctx.found;
}

// Run one press on this peer's copy with `player` as the presser, the presser's look-at flags written
// over ours for the call. True when the verb dispatched.
bool Run(void* actor, void* player, const coop::net::InteractForwardPayload& p) {
    if (p.verb > V::kMax) return false;
    void* fn = R::FindDispatchFunctionCached(R::ClassOf(actor), kVerbs[p.verb]);
    if (!fn) return false;
    const std::wstring compName = coop::net::StringFromWireKey(p.component);
    void* comp = compName.empty() ? nullptr : ue_wrap::hook::ComponentByName(actor, compName.c_str());
    const auto& fields = LookFields(R::ClassOf(actor));
    std::array<uint8_t, coop::net::kInteractFlags> saved{};
    auto* bytes = static_cast<uint8_t*>(actor);
    const int n = std::min<int>(p.flagCount, coop::net::kInteractFlags);
    std::vector<int32_t> written;
    for (int i = 0; i < n; ++i) {
        const int32_t off = p.flagOffset[i];
        bool known = false;
        for (int32_t f : fields) known = known || f == off;
        if (!known) continue;  // only the class's own look-at fields are ever written
        saved[written.size()] = bytes[off];
        bytes[off] = p.flagValue[i] ? 1 : 0;
        written.push_back(off);
    }
    ue_wrap::ParamFrame f(fn);
    bool ok = f.valid();
    if (ok) {
        f.Set<void*>(L"player", player);
        f.Set<void*>(L"Player", player);
        f.Set<uint8_t>(L"action", p.action);
        if (comp) f.Set<void*>(L"lookAtComponent", comp);
        if (R::FindParamOffset(fn, L"hit") >= 0)
            ue_wrap::hit_result::Write(f, L"hit", actor, comp, ue_wrap::FVector{p.hitX, p.hitY, p.hitZ});
        ok = ue_wrap::Call(actor, f);
    }
    if (R::IsLive(actor))
        for (size_t i = 0; i < written.size(); ++i) bytes[written[i]] = saved[i];
    return ok;
}

// ---- the press seam ----------------------------------------------------------------------------
sg::Verdict OnPressPre(const sg::Call& c) {
    auto* s = g_session;
    if (!s || !s->connected() || c.fromOurCode || !c.object) return sg::Verdict::Run;
    const Policy policy = PolicyOf(c.object);
    if (policy == Policy::None || !coop::net_pump::IsInAnnouncedWorld(c.object)) return sg::Verdict::Run;
    const uint8_t verb = static_cast<uint8_t>(c.tag - kTagBase);
    if (verb > V::kMax) return sg::Verdict::Run;
    void* player = nullptr;
    if (!ReadParam(c, L"player", player) && !ReadParam(c, L"Player", player)) return sg::Verdict::Run;
    if (player != coop::players::Registry::Get().Local()) return sg::Verdict::Run;  // not this peer's own press
    if (verb == V::kPlayerUsedOn) {
        void* held = nullptr;
        if (ReadParam(c, L"holdObject", held) && held) return sg::Verdict::Run;  // an item used on it: its own lanes
    }
    const bool host = s->role() == coop::net::Role::Host;
    if (host && policy == Policy::HostOnly) return sg::Verdict::Run;  // the host's world is the world

    coop::net::InteractForwardPayload p{};
    const std::wstring cls = R::ClassNameOf(c.object);
    const std::wstring key = coop::element::PortableWireKey(c.object);
    if (key.empty() || cls.size() > sizeof(p.className.data)) return sg::Verdict::Run;  // nameless: stays local
    p.className.len = static_cast<uint8_t>(cls.size());
    for (size_t i = 0; i < cls.size(); ++i) p.className.data[i] = static_cast<char>(cls[i]);
    coop::net::WireKeyFromString(key, p.key);
    p.verb = verb;
    uint8_t action = 0;
    if (ReadParam(c, L"action", action)) p.action = action;
    void* comp = nullptr;
    if (ReadParam(c, L"lookAtComponent", comp) && comp && R::IsLive(comp)) {
        const std::wstring cn = R::ToString(R::NameOf(comp));
        if (cn.size() <= sizeof(p.component.data)) coop::net::WireKeyFromString(cn, p.component);
    }
    const int hitOff = R::FindParamOffset(c.function, L"hit");
    ue_wrap::hit_result::Hit hit;
    if (c.locals && hitOff >= 0 &&
        ue_wrap::hit_result::Parse(c.locals + hitOff, R::FindParamSize(c.function, L"hit"), &hit)) {
        p.hitX = hit.location.X; p.hitY = hit.location.Y; p.hitZ = hit.location.Z;
    }
    const auto* bytes = static_cast<const uint8_t*>(c.object);
    for (int32_t off : LookFields(R::ClassOf(c.object))) {
        if (p.flagCount >= coop::net::kInteractFlags) break;
        p.flagOffset[p.flagCount] = static_cast<uint16_t>(off);
        p.flagValue[p.flagCount] = bytes[off] ? 1 : 0;
        ++p.flagCount;
    }
    p.presserSlot = coop::players::Registry::Get().LocalPeerId();
    const bool sent = s->SendReliable(coop::net::ReliableKind::InteractForward, &p, sizeof(p));
    static uint32_t s_said = 0;
    if (++s_said <= 40 || s_said % 50 == 0)
        UE_LOGI("[INTERACT-FWD] %s pressed %ls (verb %u action %u, %s) -> %s", host ? "HOST" : "CLIENT",
                cls.c_str(), verb, p.action, policy == Policy::HostOnly ? "host-only" : "everywhere",
                sent ? "sent" : "NOT sent");
    // A HostOnly press is the host's to perform; an Everywhere press runs here too.
    return (!host && policy == Policy::HostOnly) ? sg::Verdict::Cancel : sg::Verdict::Run;
}

// Per sender: a small token bucket, so a stuck key cannot flood the host's game thread.
struct Bucket { float tokens = kPressesPerSecond; uint64_t at = 0; };
std::array<Bucket, coop::net::kMaxPeers> g_rate{};
bool Take(uint8_t slot) {
    Bucket& b = g_rate[slot];
    const uint64_t now = NowMs();
    if (b.at) b.tokens = std::min<float>(kPressesPerSecond, b.tokens + kPressesPerSecond * (now - b.at) / 1000.f);
    b.at = now;
    if (b.tokens < 1.f) return false;
    b.tokens -= 1.f;
    return true;
}

}  // namespace

void Install(coop::net::Session* session) {
    g_session = session;
    if (g_installed) return;
    g_installed = true;
    int ok = 0;
    for (int v = 0; v <= V::kMax; ++v)
        if (sg::WatchName(kVerbs[v], kTagBase + v, &OnPressPre, nullptr)) ++ok;
    UE_LOGI("[INTERACT-FWD] generic interaction lane installed: %d/3 verb watches, %zu census classes",
            ok, std::size(kClasses));
}

void OnPress(coop::net::Session& session, const coop::net::InteractForwardPayload& in, uint8_t senderSlot) {
    if (!session.connected() || in.verb > V::kMax || in.flagCount > coop::net::kInteractFlags ||
        in.className.len > sizeof(in.className.data) || in.key.len > sizeof(in.key.data) ||
        in.component.len > sizeof(in.component.data)) return;
    coop::net::InteractForwardPayload p = in;
    const bool host = session.role() == coop::net::Role::Host;
    if (host) {
        if (senderSlot < 1 || senderSlot >= coop::net::kMaxPeers || !Take(senderSlot)) return;
        p.presserSlot = senderSlot;  // the wire's word for who pressed is the host's, never the sender's
    } else if (senderSlot != 0 || p.presserSlot >= coop::net::kMaxPeers) {
        return;
    }
    void* actor = Resolve(p);
    if (!actor) {
        UE_LOGW("[INTERACT-FWD] press from slot %u: target %.*s '%.*s' not found here", p.presserSlot,
                static_cast<int>(p.className.len), p.className.data, static_cast<int>(p.key.len), p.key.data);
        return;
    }
    const Policy policy = PolicyOf(actor);
    if (policy == Policy::None) return;  // only the census classes ride this lane, whatever a sender says
    if (host) {
        const auto subject = coop::element::IntentTarget::ForClientIntent(session, senderSlot, kReachUU).Authorize(actor);
        if (!subject) {
            UE_LOGI("[INTERACT-FWD] DENY slot %u on %.*s -- %s", senderSlot, static_cast<int>(p.className.len),
                    p.className.data,
                    subject.outcome == coop::element::IntentOutcome::NoBody ? "no body yet" : "out of reach");
            return;
        }
    }
    void* player = PlayerFor(p.presserSlot);
    if (!player) {
        UE_LOGW("[INTERACT-FWD] press from slot %u: no puppet to play it", p.presserSlot);
        return;
    }
    const bool ran = Run(actor, player, p);
    UE_LOGI("[INTERACT-FWD] %s ran slot %u's press on %.*s (verb %u action %u): %s", host ? "HOST" : "CLIENT",
            p.presserSlot, static_cast<int>(p.className.len), p.className.data, p.verb, p.action,
            ran ? "ok" : "did not dispatch");
    if (!host || !ran) return;
    // An Everywhere press is replayed by every other peer; a HostOnly one is the host's alone, and its
    // record goes out so every peer, the presser included, takes the result.
    if (policy == Policy::Everywhere)
        session.SendReliable(coop::net::ReliableKind::InteractForward, &p, sizeof(p), /*skipSlot=*/senderSlot);
    if (R::IsLive(actor) && coop::prop_save_data::Covers(actor)) {
        const std::wstring key = ue_wrap::prop::GetInteractableKeyString(actor);
        if (!key.empty() && key != L"None") coop::prop_save_data::Publish(&session, actor, key);
    }
}

void OnDisconnect() {
    g_session = nullptr;
    g_rate = {};
}

}  // namespace coop::interact_forward
