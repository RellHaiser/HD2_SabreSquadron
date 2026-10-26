// A place to deliver documents (C_DocumentPlace, actor type 23, vtable 0x008135e0, 0x150 bytes;
// original 0x0060dab0..0x0060ed90): an objective a human brings an item (the documents) to. While
// it waits (state 0) its frame shows and a box volume (COLLISION_TYPE_LIGHT_OBJECT) lets the use
// test find it; delivered (1) the documents' model is left lying in its place and the frame hides;
// 2 hides everything; 3 is the state between missions.
#pragma once

#include <cstdint>

#include "game/actor.h"
#include "game/collision.h"
#include "ls3d/math.h"

class C_Human;

enum : int {
    DOCUMENTS_WAITING = 0,
    DOCUMENTS_DELIVERED = 1,
    DOCUMENTS_HIDDEN = 2,
    DOCUMENTS_STOPPED = 3,                              // (also the state before the first mission)
};

class alignas(16) C_DocumentPlace : public C_Actor {    // "Document place", vtable 0x008135e0
public:
    C_DocumentPlace();                                  // 0x0060db70
    ~C_DocumentPlace() override;                        // 0x0060dbf0 (scalar deleting 0x0060dbd0)

    // The script's state (DOCUMENTS_*), the clients told unless the game is local.
    void SetState(int state);                           // 0x0060eac0
    int GetState() const { return m_state; }            // +0x148
    // `human` delivers the documents it carries (while waiting): they leave his inventory, the
    // clients are told, the place shows them.
    bool Deliver(C_Human *human);                       // 0x0060e9b0
    // The item type of the documents.
    int GetItemId() const { return m_itemId; }          // +0x98
    // The text of its signal.
    int GetTextId() const { return m_textId; }          // +0x9C

    // A delivery on a client (the documents leave the deliverer's inventory there too).
    void OnMessage(C_BitStream *msg) override;          // 0x1C 0x0060e420
    // (Update 0x0060dc60, Tick 0x0060dc50 and slot 0x20 0x0060e4c0 are empty and SetFrame
    // 0x0060dc00 is the base's, as are the base's.)
    bool WriteProperties(C_chunk &chunk) override;      // 0x30 0x0060df50
    bool ReadProperties(C_chunk &chunk) override;       // 0x34 0x0060e160
    void CopyFrom(const C_Actor *source) override;      // 0x38 0x0060dc10
    // Once until StopMission: the box volume made, waiting.
    void StartMission(bool server) override;            // 0x3C 0x0060dc70
    void StopMission(bool server) override;             // 0x40 0x0060df20
    // Each side the game runs is stopped and started again.
    void RestartMission(bool server) override;          // 0x44 0x0060e560
    // Chunk 3000: the state (not the base's actor chunk).
    bool SaveGame(C_chunk &chunk) override;             // 0x48 0x0060e5b0
    bool LoadGame(C_chunk &chunk) override;             // 0x4C 0x0060e6b0
    void WriteState(C_BitStream *msg) override;         // 0x50 0x0060e4d0
    // Delivered: only the documents shown (quirk kept: the state stays).
    void ReadState(C_BitStream *msg) override;          // 0x54 0x0060e510
    int GetMaterialId(I3D_frame *) override { return 85; }   // 0xD8 0x0060e550

private:
    // The state's frames, volume and name (nothing when it is the state already).
    void ApplyState(int state);                         // 0x0060eb60
    // The documents' model (made once from the item's) put at the frame, unlinked into the world
    // there, the frame hidden.
    void ShowDocuments();                               // 0x0060e860
    // A client: `humanId` delivered the item `uid`.
    void OnDelivered(int humanId, int uid);             // 0x0060ea70

    bool m_started = false;                             // +0x90
    int m_nameTextId = 0x4E7;                           // +0x94 the name while waiting
    int m_itemId = 0x44;                                // +0x98
    int m_textId = 0x1B8B;                              // +0x9C
    S_CollisionVolume m_volume;                         // +0xA0
    S_matrix m_invMatrix;                               // +0x100 the frame's inverse world matrix (the volume's)
    bool m_volumeAdded = false;                         // +0x140
    I3D_frame *m_documents = nullptr;                   // +0x144 the documents' visual
    int m_state = DOCUMENTS_STOPPED;                    // +0x148
};

// The document places' creator (g_actorCreators[ACTOR_DOCUMENT_PLACE], vtable 0x008135d0).
class C_DocumentPlaceCreator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x0060dae0
    const char *GetName() override { return "Document place"; }   // 0x0060db40
    void Destroy(C_Actor *actor) override;              // 0x0060db50
};
