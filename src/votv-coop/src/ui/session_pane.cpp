// ui/session_pane.cpp -- see ui/session_pane.h.

#include <winsock2.h>  // before anything that may pull in windows.h
#include <ws2tcpip.h>

#include "ui/session_pane.h"

#include "coop/config/config.h"
#include "coop/config/config_registry.h"
#include "coop/creatures/npc_sync.h"     // GetSession: the running session, set at its start
#include "coop/net/net_stats.h"
#include "coop/net/session.h"
#include "coop/player/roster.h"          // LocalIsHost
#include "coop/session/session_manager.h"
#include "coop/session/subsystems.h"     // ConnectReplayForSlot
#include "l10n/l10n.h"
#include "ue_wrap/core/game_thread.h"
#include "ue_wrap/core/log.h"

#include "imgui.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace ui::session_pane {
namespace {

namespace NS = coop::net::net_stats;
namespace GT = ue_wrap::game_thread;
using Clock = std::chrono::steady_clock;

// A resync is the whole connect replay per player: a bracket of every prop and each lane's state.
// The host's button is paced so a double click cannot stack two brackets.
constexpr auto kResyncCooldown = std::chrono::seconds(30);
Clock::time_point g_lastResync{};
std::string g_resyncNote;

// This machine's IPv4 addresses (LAN, Tailscale, ZeroTier, Hamachi...), read once per pane open:
// the friend types one of them with the port into Direct Connect.
std::vector<std::string> g_addrs;
Clock::time_point g_addrsAt{};

void ReadAddresses() {
    g_addrs.clear();
    char host[256] = {};
    if (gethostname(host, sizeof(host)) != 0) return;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo* res = nullptr;
    if (getaddrinfo(host, nullptr, &hints, &res) != 0 || !res) return;
    for (addrinfo* a = res; a; a = a->ai_next) {
        char ip[INET_ADDRSTRLEN] = {};
        const auto* sin = reinterpret_cast<const sockaddr_in*>(a->ai_addr);
        if (!inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip))) continue;
        const std::string s(ip);
        if (s.rfind("127.", 0) == 0 || s.rfind("169.254.", 0) == 0) continue;
        bool seen = false;
        for (const auto& x : g_addrs) seen = seen || x == s;
        if (!seen) g_addrs.push_back(s);
    }
    freeaddrinfo(res);
}

// A short hint for the address kinds friends recognise.
const char* KindOf(const std::string& ip) {
    if (ip.rfind("100.", 0) == 0) return "Tailscale";
    if (ip.rfind("192.168.", 0) == 0 || ip.rfind("10.", 0) == 0) return "LAN";
    if (ip.rfind("25.", 0) == 0 || ip.rfind("26.", 0) == 0) return "Hamachi";
    if (ip.rfind("172.", 0) == 0) return "LAN/ZeroTier";
    return "";
}

void RenderHost(const NS::Snapshot& s) {
    ImGui::SeparatorText(l10n::T("Invite"));
    if (g_addrs.empty() || Clock::now() - g_addrsAt > std::chrono::seconds(10)) {
        ReadAddresses();
        g_addrsAt = Clock::now();
    }
    const int port = coop::config::ResolveInt(::coop::config_registry::rows::net_port);
    ImGui::TextWrapped("%s", l10n::T("Friends on the same network or VPN (Tailscale, ZeroTier, Hamachi) join with "
                                     "Direct Connect. Click an address to copy it."));
    if (g_addrs.empty()) ImGui::TextDisabled("%s", l10n::T("No network address found."));
    for (const auto& ip : g_addrs) {
        char line[96];
        std::snprintf(line, sizeof(line), "%s:%d", ip.c_str(), port);
        ImGui::PushID(line);
        if (ImGui::Button(line)) {
            ImGui::SetClipboardText(line);
            UE_LOGI("session_pane: copied invite address to the clipboard");
        }
        const char* kind = KindOf(ip);
        if (*kind) { ImGui::SameLine(); ImGui::TextDisabled("%s", kind); }
        ImGui::PopID();
    }
    const std::string lobby = coop::session_manager::OwnLobbyId();
    if (!lobby.empty()) {
        ImGui::TextDisabled("%s", l10n::T("Over the internet, friends find you in the server browser."));
    }

    ImGui::SeparatorText(l10n::T("Resync"));
    ImGui::TextWrapped("%s", l10n::T("Sends the whole world state to every joined player again: props, doors, power, "
                                     "the drone, the computer. Use it when something looks different for a friend."));
    const auto left = kResyncCooldown - (Clock::now() - g_lastResync);
    const bool cooling = g_lastResync != Clock::time_point{} && left > Clock::duration::zero();
    if (cooling) ImGui::BeginDisabled();
    if (ImGui::Button(l10n::T("Resync all players")) && s.connected) {
        g_lastResync = Clock::now();
        g_resyncNote = l10n::T("Resync sent.");
        GT::Post([] {
            auto* session = coop::npc_sync::GetSession();
            if (!session || session->role() != coop::net::Role::Host || !session->connected()) return;
            int n = 0;
            for (int slot = 1; slot < static_cast<int>(coop::net::kMaxPeers); ++slot) {
                if (!session->HasPeerConn(slot) || !session->IsSlotWorldReady(slot)) continue;
                coop::subsystems::ConnectReplayForSlot(slot);
                ++n;
            }
            UE_LOGI("session_pane: host resync -- connect replay re-run for %d player(s)", n);
        });
    }
    if (cooling) {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%llds",
            static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(left).count() + 1));
    }
    if (!g_resyncNote.empty()) ImGui::TextDisabled("%s", g_resyncNote.c_str());
}

char g_address[128] = {};
bool g_addressSeeded = false;

void Dial(const std::string& address) {
    GT::Post([address] {
        coop::net::Refusal refused;
        const bool ok = coop::session_manager::ConnectDirect(address, &refused);
        if (ok) coop::config::SetValue(::coop::config_registry::rows::browser_lastdirect, address.c_str());
        UE_LOGI("session_pane: quick connect %s", ok ? "queued" : "refused");
    });
}

void RenderMenu() {
    ImGui::SeparatorText(l10n::T("Quick connect"));
    if (!g_addressSeeded) {
        g_addressSeeded = true;
        const std::string last =
            coop::config::ResolveString(::coop::config_registry::rows::browser_lastdirect);
        std::snprintf(g_address, sizeof(g_address), "%s", last.c_str());
    }
    ImGui::TextWrapped("%s", l10n::T("Paste the address your host copied (host:port) and connect. "
                                     "The last address you joined is filled in."));
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.f);
    ImGui::InputText("##qc_address", g_address, sizeof(g_address));
    ImGui::SameLine();
    if (ImGui::Button(l10n::T("Paste"))) {
        if (const char* clip = ImGui::GetClipboardText())
            std::snprintf(g_address, sizeof(g_address), "%s", clip);
    }
    ImGui::SameLine();
    const bool empty = g_address[0] == '\0';
    if (empty) ImGui::BeginDisabled();
    if (ImGui::Button(l10n::T("Connect"))) Dial(g_address);
    if (empty) ImGui::EndDisabled();
}

}  // namespace

void Render() {
    NS::Snapshot s;
    NS::Get(s);
    const bool host = coop::roster::LocalIsHost();
    if (s.connected) {
        char line[160];
        l10n::Fmt(line, sizeof(line), l10n::T("Connected as %s, %d connection(s), worst ping %d ms"),
                  host ? l10n::T("host") : l10n::T("client"), s.peers, s.pingMaxMs < 0 ? 0 : s.pingMaxMs);
        ImGui::TextUnformatted(line);
    } else {
        ImGui::TextDisabled("%s", l10n::T("Not in a session."));
    }
    if (host) RenderHost(s);  // a host alone is not "connected" yet, and it is then that it invites
    else if (!s.connected) RenderMenu();
    else ImGui::TextWrapped("%s", l10n::T("If something looks different from your host, ask the host to press "
                                          "Resync in this pane."));
}

}  // namespace ui::session_pane
