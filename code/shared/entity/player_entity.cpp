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

        // SEEDED HERE BECAUSE THEY LEFT SerializeFields.
        //
        // Movement, input and aim moved to PlayerOnFootSync,
        // which a player only sends while they are actually moving or aiming.
        // A client that streams in a player standing perfectly still would
        // otherwise never hear their pose or their gun direction at all.
        //
        // Construction is sent once per receiver, so this is the right place:
        // it is the seed, and the packets correct it from there.
        fields.Field(leftStickX);
        fields.Field(leftStickY);
        fields.Field(accelerator);
        fields.Field(brake);
        fields.Field(buttons);
        fields.Field(cameraRotation);
        fields.Field(isDucked);
        fields.Field(isInAir);
        fields.Field(isInWater);
        fields.Field(aimHorizontal);
        fields.Field(aimVertical);
    }

    void PlayerEntity::SerializeTransform(Replication::FieldSerializer &fields) {
        // A PRESENCE BIT, SO THE TWO SIDES CANNOT DISAGREE ABOUT THE SHAPE.
        //
        // Same mechanism as VehicleEntity. If the writer simply wrote nothing,
        // a reader would still try to read three vectors and a quat out of
        // whatever came next - and this channel carries no field names, so it
        // would not fail, it would silently deserialize garbage into the pose.
        //
        // On the read side `present` is whatever the SENDER wrote, never this
        // entity's own onFootSyncActive: a receiver is not the owner and has
        // no business deciding the shape of someone else's message.
        bool present = !onFootSyncActive;

        fields.Field(present);

        if (!present) {
            return;
        }

        fields.Field(position);
        fields.Field(velocity);
        fields.Field(rotation);
    }

    void PlayerEntity::SerializeFields(Replication::FieldSerializer &fields) {
        // ORDER IS THE WIRE FORMAT. Both sides walk these in the same sequence
        // with no field names, so inserting a field anywhere but the end makes
        // every field after it deserialize as the wrong thing - and it will not
        // fail, it will just be wrong. Append only.
        //
        // MOVEMENT, INPUT AND AIM ARE NOT HERE ANY MORE. leftStickX/Y,
        // accelerator, brake, buttons, cameraRotation, the three pose flags
        // and the two aim angles used to ride this snapshot every tick. They
        // are now carried by PlayerOnFootSync, which names
        // them in a change mask and quantizes them - keeping them here too
        // would be the same fact on the wire twice.
        //
        // The FIELDS still exist on the class: the server holds them as
        // authoritative state and construction seeds a late joiner from them.
        // What changed is that they no longer re-serialize per tick.
        fields.Field(health);
        fields.Field(armour);

        fields.Field(weaponType);
        fields.Field(ammoClip);
        fields.Field(ammoTotal);

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

        fields.Field(walkingStyle);
        fields.Field(alpha);
        fields.Field(onFire);

        fields.Field(maxHealth);
        fields.Field(isDrowning);
        fields.Field(hasJetPack);
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
