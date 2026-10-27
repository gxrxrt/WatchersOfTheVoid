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
#include <vector>

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

// Clawd, Claude Code's mascot, exactly as its terminal banner draws him: three lines of quadrant
// block characters, each character a 2x2 block of pixels. Decoded into an 18x6 pixel grid and built
// from the engine's cube, one box per horizontal run, extruded to a desk-figure depth. The two holes
// the banner leaves in his top row (the missing quadrants of the corner characters) are his eyes.
constexpr const wchar_t* kClawd[] = {L" \x2590\x259B\x2588\x2588\x2588\x259C\x258C ",
                                     L"\x259D\x259C\x2588\x2588\x2588\x2588\x2588\x259B\x2598",
                                     L"  \x2598\x2598 \x259D\x259D  "};
constexpr int kCols = 18, kRows = 6;
constexpr float kVoxel = 1.8f, kDepth = 3.f * kVoxel;  // centimetres
constexpr ue_wrap::FLinearColor kEyes{0.01f, 0.01f, 0.01f, 1.f};

// UL=1, UR=2, LL=4, LR=8.
uint8_t Quadrants(wchar_t ch) {
    switch (ch) {
    case wchar_t(0x2588): return 15; case wchar_t(0x2590): return 2 | 8; case wchar_t(0x258C): return 1 | 4;
    case wchar_t(0x259B): return 1 | 2 | 4; case wchar_t(0x259C): return 1 | 2 | 8;
    case wchar_t(0x259D): return 2; case wchar_t(0x2598): return 1;
    default: return 0;
    }
}

struct Box { int row, col, len; bool eye; };
std::vector<Box> ClawdBoxes() {
    bool body[kRows][kCols] = {}, eye[kRows][kCols] = {};
    for (int line = 0; line < 3; ++line) {
        for (int c = 0; c < kCols / 2; ++c) {
            const wchar_t ch = kClawd[line][c];
            const uint8_t q = Quadrants(ch);
            const bool corner = ch == wchar_t(0x259B) || ch == wchar_t(0x259C);
            for (int sub = 0; sub < 4; ++sub) {
                const int r = line * 2 + sub / 2, col = c * 2 + sub % 2;
                if (q & (1 << sub)) body[r][col] = true;
                else if (corner && line == 0) eye[r][col] = true;
            }
        }
    }
    std::vector<Box> out;
    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kCols;) {
            if (!body[r][c]) { if (eye[r][c]) out.push_back({r, c, 1, true}); ++c; continue; }
            int e = c;
            while (e < kCols && body[r][e]) ++e;
            out.push_back({r, c, e - c, false});
            c = e;
        }
    }
    return out;
}

// Build Clawd on the plush: its own mesh is hidden (its collision and physics stay, so he is carried,
// thrown and synced like any prop) and the cubes ride its root. False when the engine shapes did not
// load or no cube attached.
bool BuildClawd(void* actor) {
    static void* cube = ue_wrap::asset_load::LoadObjectByPath(L"/Engine/BasicShapes/Cube.Cube");
    static void* material = ue_wrap::asset_load::LoadObjectByPath(kColorMaterial);
    static void* smc = R::FindClass(L"StaticMeshComponent");
    void* add = R::FindDispatchFunctionCached(R::ClassOf(actor), L"AddComponentByClass");
    if (!cube || !material || !smc || !add) return false;
    int built = 0;
    for (const Box& b : ClawdBoxes()) {
        ue_wrap::ParamFrame f(add);
        ue_wrap::FTransform t{};
        t.TY = ((b.col + b.len * 0.5f) - kCols * 0.5f) * kVoxel;
        t.TZ = (kRows * 0.5f - (b.row + 0.5f)) * kVoxel;
        t.SX = kDepth / 100.f; t.SY = b.len * kVoxel / 100.f; t.SZ = kVoxel / 100.f;
        void* comp = nullptr;
        if (!f.valid() || !f.Set<void*>(L"Class", smc) || !f.Set<bool>(L"bManualAttachment", false) ||
            !f.Set<ue_wrap::FTransform>(L"RelativeTransform", t) || !f.Set<bool>(L"bDeferredFinish", false) ||
            !ue_wrap::Call(actor, f) || !f.GetRaw(L"ReturnValue", &comp, sizeof(comp)) || !comp) continue;
        ue_wrap::ParamFrame collision(R::FindDispatchFunctionCached(R::ClassOf(comp), L"SetCollisionEnabled"));
        if (collision.valid() && collision.Set<uint8_t>(L"NewType", 0)) ue_wrap::Call(comp, collision);
        E::SetStaticMesh(comp, cube);
        E::SetComponentMaterial(comp, 0, material);
        if (void* mid = E::CreateDynamicMaterialInstance(comp, 0)) SetColor(mid, L"Color", b.eye ? kEyes : kTerracotta);
        ++built;
    }
    if (built == 0) return false;
    if (void* mesh = E::GetStaticMeshComponent(actor)) E::SetComponentVisible(mesh, false, /*propagate=*/false);
    return true;
}

bool Dress(void* actor) {
    if (BuildClawd(actor)) return true;
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
