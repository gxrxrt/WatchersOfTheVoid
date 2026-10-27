#pragma once
#include <cstdint>
namespace coop::net { class Session; struct KitchenStatePayload; struct KitchenIntentPayload; struct KitchenDoorPosePayload; }
namespace coop::kitchen_sync {
void Install(net::Session* session);
void Tick();
void Apply(const net::KitchenStatePayload& p);
void OnIntent(const net::KitchenIntentPayload& p, uint8_t senderSlot);
// HOST: a client's dragged cremator door pose (KitchenDoorPose).
void OnDoorPose(const net::KitchenDoorPosePayload& p, uint8_t senderSlot);
void SendAllToSlot(int slot);
void OnPeerLeft(int slot);
bool IsLocalLidOutput(void* source);
bool PublishCurrent(void* actor);
void OnDisconnect();
}
