#pragma once

#include <mafianet/BitStream.h>

#include <cstdint>

namespace VantaMP::Shared::RPC {
    // A player is getting out of the vehicle they occupy.
    //
    // Carries no vehicle id: a player occupies at most one, and both sides
    // already know which. Sending it would create a second source of truth
    // that could disagree with the first.
    struct PlayerExitVehicle {
        static constexpr const char *kIdentifier = "VantaMP::PlayerExitVehicle";

        std::uint64_t networkId = 0;
        bool          warp      = false;
        // Same GTA door the owner opened. 0xFF lets the receiver compute it.
        std::uint8_t door = 0xFF;
        // Walk-away / denied climb, not a leave. The server must not fire
        // vehicleStartExit or a resource can trap them mid-walk.
        bool abortEnter = false;

        // THE DOOR'S ANGLE AT THE MOMENT THE ENTRY ENDED, 0..255.
        //
        // An aborted entry leaves the door part way open, and how far depends
        // on when the player bailed. Nobody else can derive that: the door is
        // masked from network ratio writes for as long as a ped is using it
        // (see SDK::OccupiedDoorMask), and the owner's ratio reports stop the
        // instant the enter task ends - which is BEFORE GTA has finished
        // swinging the door. So the last thing the other clients heard was a
        // stale mid-climb angle, and they kept whatever they had.
        //
        // MTA carries the ratio inside the abort packet for exactly this
        // reason: CClientPed.cpp:10443 writes GetDoorOpenRatio(enteringDoor)
        // into the notify-abort, and CPacketHandler.cpp:1972-1974 re-allows
        // the door and forces that value on receipt. This is the same fact on
        // the same packet.
        std::uint8_t doorRatio = 0;

        // GTA THREW THEM OUT; THEY DID NOT GET OUT.
        //
        // A crash on a bike raises CEventKnockOffBike (World.cpp:2008) and the
        // rider is ejected without ever running a leave task. That arrives as
        // warp=true, because they are already out - but so does a
        // server-forced warp-out, and the two should not look the same. This
        // bit tells the receiver to play GTA's fall rather than place the ped
        // at the door.
        bool knockedOff = false;

        void FromParameters(std::uint64_t id, bool instant, std::uint8_t usedDoor = 0xFF, bool cancelEnter = false, std::uint8_t ratio = 0, bool thrown = false) {
            networkId  = id;
            warp       = instant;
            door       = usedDoor;
            abortEnter = cancelEnter;
            doorRatio  = ratio;
            knockedOff = thrown;
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, networkId);
            bs->Serialize(write, warp);
            bs->Serialize(write, door);
            bs->Serialize(write, abortEnter);
            bs->Serialize(write, doorRatio);
            bs->Serialize(write, knockedOff);
        }
    };
} // namespace VantaMP::Shared::RPC
