// coop/fun/claude_figurine.cpp -- see coop/fun/claude_figurine.h.

#include "coop/fun/claude_figurine.h"

#include "coop/player/players_registry.h"
#include "coop/props/prop_element_tracker.h"  // FindLiveActorByKey
#include "coop/props/prop_fresh_spawn.h"      // PropSetKeyFn
#include "ue_wrap/core/asset_load.h"
#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/fname_utils.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_component.h"

#include <array>
#include <chrono>
#include <string>

namespace coop::claude_figurine {
namespace {

namespace R = ue_wrap::reflection;
namespace E = ue_wrap::engine;
using Clock = std::chrono::steady_clock;

constexpr const wchar_t* kPlushClass = L"/Game/objects/prop_eriePlush.prop_eriePlush_C";
// The engine's basic-shape material: a plain lit surface with one Color parameter, so the plush
// takes Claude's colour whatever its own material's parameters are called.
constexpr const wchar_t* kColorMaterial = L"/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial";
constexpr int kMaxFigurines = 16;
// Claude's terracotta, #D97757, in linear space.
constexpr ue_wrap::FLinearColor kTerracotta{0.693f, 0.184f, 0.107f, 1.f};

std::array<ue_wrap::CachedObjRef, kMaxFigurines> g_dressed{};
Clock::time_point g_nextTick{};
bool g_materialSaid = false;

std::wstring KeyOf(int i) { return L"claude_fig_" + std::to_wstring(i + 1); }

bool SetColor(void* mid, const wchar_t* param, const ue_wrap::FLinearColor& c) {
    void* fn = mid ? R::FindDispatchFunctionCached(R::ClassOf(mid), L"SetVectorParameterValue") : nullptr;
    if (!fn) return false;
    ue_wrap::ParamFrame f(fn);
    const R::FName name = ue_wrap::fname_utils::StringToFName(param);
    return f.valid() && f.SetRaw(L"ParameterName", &name, sizeof(name)) &&
           f.SetRaw(L"Value", &c, sizeof(c)) && ue_wrap::Call(mid, f);
}

bool Dress(void* actor) {
    void* mesh = E::GetStaticMeshComponent(actor);
    if (!mesh) return false;
    static void* material = nullptr;
    if (!material) material = ue_wrap::asset_load::LoadObjectByPath(kColorMaterial);
    if (material) {
        for (int32_t slot = 0; slot < 4; ++slot) E::SetComponentMaterial(mesh, slot, material);
    } else if (!g_materialSaid) {
        g_materialSaid = true;
        UE_LOGW("claude_figurine: the basic-shape material did not load -- tinting the plush's own material");
    }
    bool tinted = false;
    for (int32_t slot = 0; slot < 4; ++slot) {
        void* mid = E::CreateDynamicMaterialInstance(mesh, slot);
        if (!mid) continue;
        for (const wchar_t* p : {L"Color", L"BaseColor", L"Tint", L"color"}) tinted = SetColor(mid, p, kTerracotta) || tinted;
    }
    return tinted;
}

}  // namespace

bool SpawnNearLocalPlayer() {
    void* player = coop::players::Registry::Get().Local();
    ue_wrap::FVector at{};
    if (!player || !E::TryGetActorLocation(player, at)) return false;
    int free = -1;
    for (int i = 0; i < kMaxFigurines && free < 0; ++i)
        if (!coop::prop_element_tracker::FindLiveActorByKey(KeyOf(i))) free = i;
    if (free < 0) { UE_LOGW("claude_figurine: all %d figurines already exist", kMaxFigurines); return false; }
    void* cls = ue_wrap::asset_load::LoadObjectByPath(kPlushClass);
    void* setKey = coop::prop_fresh_spawn::PropSetKeyFn();
    if (!cls || !setKey) { UE_LOGW("claude_figurine: the plush class or setKey did not resolve"); return false; }
    const ue_wrap::FVector fwd = E::GetActorForwardVector(player);
    const ue_wrap::FVector loc{at.X + fwd.X * 90.f, at.Y + fwd.Y * 90.f, at.Z + 40.f};
    const ue_wrap::FRotator rot{};
    void* actor = E::BeginDeferredSpawn(cls, loc, rot);
    if (!actor) return false;
    // The Key before Finish, so the prop's own Init keeps it instead of minting a GUID, and the
    // ordinary birth express carries it to every peer.
    const R::FName key = ue_wrap::fname_utils::StringToFName(KeyOf(free));
    ue_wrap::ParamFrame sk(setKey);
    const bool keyed = sk.SetRaw(L"Key", &key, sizeof(key)) && ue_wrap::Call(actor, sk);
    if (!E::FinishDeferredSpawn(actor, loc, rot)) return false;
    const bool dressed = Dress(actor);
    if (dressed) g_dressed[free].Set(actor);
    UE_LOGI("claude_figurine: spawned '%ls' (keyed=%d dressed=%d) -- hi!", KeyOf(free).c_str(),
            keyed ? 1 : 0, dressed ? 1 : 0);
    return true;
}

void Tick() {
    const auto now = Clock::now();
    if (now < g_nextTick) return;
    g_nextTick = now + std::chrono::seconds(2);
    for (int i = 0; i < kMaxFigurines; ++i) {
        void* actor = coop::prop_element_tracker::FindLiveActorByKey(KeyOf(i));
        if (!actor || g_dressed[i].Is(actor)) continue;
        if (Dress(actor)) {
            g_dressed[i].Set(actor);
            UE_LOGI("claude_figurine: dressed '%ls'", KeyOf(i).c_str());
        }
    }
}

}  // namespace coop::claude_figurine
