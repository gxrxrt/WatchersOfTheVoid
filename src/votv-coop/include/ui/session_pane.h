// ui/session_pane.h -- F1 > Network > Session: the everyday session tools for every player.
//
// The host's invite (this machine's addresses with the session port, one click to copy) and its
// resync of every joined player (the connect replay run again, the same answer a world-change
// re-announce gets); a player in the menu dials the last address it joined, or one it pastes.
// Render thread; every engine or session act is posted to the game thread.

#pragma once

namespace ui::session_pane {

void Render();

}  // namespace ui::session_pane
