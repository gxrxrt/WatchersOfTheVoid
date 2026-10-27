// coop/interactables/interact_forward.h -- the generic interaction lane: one wrapper over every press
// (actionOptionIndex, playerUsedOn with an empty hand, player_use) on the classes no dedicated lane
// covers.
//
// The class list and each class's policy are generated from the Blueprint census
// (interact_forward_classes.inc). Everywhere: the press runs on its presser and is replayed on every
// other peer with the presser's puppet as the player, so a lamp, a radio or a button looks the same
// to everyone. HostOnly (the class spawns or rolls): a client's press is cancelled and runs on the host
// alone, whose spawns and records already reach the peers. Excluded by the generator: classes that
// open a widget or touch the player's inventory, hand, camera or vitals, which stay each peer's own.
//
// The host is the relay: a client's press goes to the host, which runs it and passes it on. The target
// is named by its class and portable identity, which every peer derives alone.

#pragma once

#include <cstdint>

namespace coop::net {
class Session;
struct InteractForwardPayload;
}

namespace coop::interact_forward {

void Install(coop::net::Session* session);

// A press off the wire: on the host from a client (validated, run, relayed), on a client from the host
// (run with the presser's puppet). Game thread.
void OnPress(coop::net::Session& session, const coop::net::InteractForwardPayload& payload, uint8_t senderSlot);

void OnDisconnect();

}  // namespace coop::interact_forward
