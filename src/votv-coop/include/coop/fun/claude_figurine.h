// coop/fun/claude_figurine.h -- the little Claude: a collectible figurine in the game's plush family.
//
// There is no editor-made asset: the figurine is the game's own plush prop, spawned with a Key in a
// reserved family (claude_fig_1..N) and dressed on every peer in Claude's terracotta. The Key is what
// identifies it -- it rides the ordinary prop lane, the save keeps it, and a joiner's world carries
// it -- so the dressing needs no wire message: each peer dresses any live prop holding such a Key.

#pragma once

namespace coop::claude_figurine {

// HOST (or single player): spawn one in front of the local player. False when the plush class does
// not load or every Key in the family is taken. Game thread.
bool SpawnNearLocalPlayer();

// Every peer: dress live figurines not yet dressed (a 2 s cadence; a few key lookups). Game thread.
void Tick();

}  // namespace coop::claude_figurine
