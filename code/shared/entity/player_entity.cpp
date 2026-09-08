#include "player_entity.h"
#include "state_forced.h"

namespace VantaMP::Shared {
    namespace Replication = Framework::Networking::Replication;

    void PlayerEntity::OnSerializeConstruction(Replication::FieldSerializer &fields) {
        // Sent once, when the entity is first constructed on a peer. Anything
        // that does not change while a player is connected belongs here rather
        // than in SerializeFields, where it would be re-sent for ever.
        fields.Field(nickname);
        fields.Field(modelId);
    }

    void PlayerEntity::SerializeFields(Replication::FieldSerializer &fields) {
        // ORDER IS THE WIRE FORMAT. Both sides walk these in the same sequence
        // with no field names, so inserting a field anywhere but the end makes
        // every field after it deserialize as the wrong thing - and it will not
        // fail, it will just be wrong. Append only.
        fields.Field(health);
        fields.Field(armour);

        fields.Field(leftStickX);
        fields.Field(leftStickY);
        fields.Field(accelerator);
        fields.Field(brake);
        fields.Field(buttons);

        // Appended, per the note above.
        fields.Field(cameraRotation);

        fields.Field(isDucked);
        fields.Field(isInAir);
        fields.Field(isInWater);

        fields.Field(weaponType);
        fields.Field(ammoClip);
        fields.Field(ammoTotal);

        fields.Field(aimVertical);
        fields.Field(aimHorizontal);

        // On the snapshot as well as construction: construction is once, and
        // a client that missed the name (or joined before it was set) has no
        // other way to learn it. Cheap enough that re-sending it every tick
        // is fine against a string that almost never changes.
        fields.Field(nickname);

        fields.Field(isReloading);

        fields.Field(vehicleNetworkId);
        fields.Field(vehicleSeat);

        fields.Field(interior);
        fields.Field(dimension);

        fields.Field(money);
        fields.Field(wantedLevel);
        fields.Field(frozen);

        fields.Field(collisions);
        fields.Field(fightingStyle);
        fields.Field(oxygen);
    }

    Framework::Networking::Replication::NetworkEntity *PlayerEntity::GetInterestDependency() {
        if (vehicleNetworkId == 0) {
            return nullptr;
        }

        return ResolveSibling(static_cast<MafiaNet::NetworkID>(vehicleNetworkId));
    }

    void PlayerEntity::OnStateForced() {
        if (gOnPlayerForced) {
            gOnPlayerForced(this);
        }
    }
} // namespace VantaMP::Shared
