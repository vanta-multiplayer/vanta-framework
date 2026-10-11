#include "vehicle_entity.h"
#include "state_forced.h"

namespace VantaMP::Shared {
    namespace Replication = Framework::Networking::Replication;

    void VehicleEntity::SerializeTransform(Replication::FieldSerializer &fields) {
        // A PRESENCE BIT, SO THE TWO SIDES CANNOT DISAGREE ABOUT THE SHAPE.
        //
        // Suppression has to be visible in the stream. If the writer simply
        // wrote nothing, a reader would still try to read three vectors and a
        // quat out of whatever came next - and because this channel carries no
        // field names, it would not fail, it would silently deserialize
        // garbage into the pose. So the flag itself goes on the wire, one
        // bit, and the reader branches on what it actually finds rather than
        // on any local belief about who is driving.
        //
        // On the read side `present` is whatever the SENDER wrote, never this
        // entity's own driverSyncActive: a receiver is not the owner and has
        // no business deciding the shape of someone else's message.
        //
        // CONSTRUCTION IS SAFE THROUGH THE SAME PATH. The framework seeds a
        // new replica through this function too, and a suppressed seed would
        // leave a late joiner with no pose at all. It does not happen,
        // because the flag is only ever raised on the OWNER, and an owner
        // never constructs a replica for itself - construction is written by
        // the server, whose driverSyncActive for this entity is false. The
        // presence bit keeps that an invariant of the data rather than an
        // assumption: if a seed ever did arrive suppressed, the receiver
        // keeps the pose it was constructed with instead of reading rubbish.
        bool present = !driverSyncActive;

        fields.Field(present);

        if (!present) {
            return;
        }

        fields.Field(position);
        fields.Field(velocity);
        fields.Field(rotation);
    }

    void VehicleEntity::OnSerializeConstruction(Replication::FieldSerializer &fields) {
        fields.Field(modelId);
        fields.Field(primaryColour);
        fields.Field(secondaryColour);

        // SEEDED HERE BECAUSE THEY LEFT SerializeFields.
        //
        // Motion and control moved to VehicleDriverSync, which only a live
        // driver sends. A client that streams in a car nobody is driving
        // would otherwise never hear its health, engine or physical flags at
        // all - it would show a pristine, idling car that is actually a
        // burnt-out wreck with its lights on.
        //
        // Construction is sent once per receiver, so this is the right place:
        // it is the seed a receiver starts from, and the packets correct it
        // from there. Sending it per tick was the duplication being removed.
        fields.Field(health);
        fields.Field(engineOn);
        fields.Field(sirenOn);
        fields.Field(lightsOn);
        fields.Field(onGround);
        fields.Field(handbrake);
    }

    void VehicleEntity::SerializeFields(Replication::FieldSerializer &fields) {
        // ORDER IS THE WIRE FORMAT. Append only.
        //
        // MOTION AND CONTROL ARE NOT HERE ANY MORE. angularVelocity, health,
        // engineOn, sirenOn, steer, accelerator, brake, handbrake, gasPedal,
        // brakePedal, lightsOn and onGround used to ride this snapshot every
        // tick. They are now carried by VehicleDriverSync and
        // VehicleUnoccupiedSync, which name them in a change mask and
        // quantize them - so keeping them here too would be the same fact on
        // the wire twice. See docs/vehicle-protocol-migration.md.
        //
        // The FIELDS still exist on the class: the server holds them as
        // authoritative state and construction seeds a late joiner from them.
        // What changed is that they no longer re-serialize per tick.
        //
        // What remains below is configuration and script state: it changes
        // rarely, it must survive a late join, and the VDS delta costs one bit
        // per unchanged field - cheaper than any packet, because an RPC pays a
        // Huffman-encoded identifier that the replica path does not.
        fields.Field(damageEngine);
        fields.Field(doorsPacked);
        fields.Field(wheelsPacked);
        fields.Field(panels);
        fields.Field(lights);
        fields.Field(doorRatiosPacked);
        fields.Field(blown);

        fields.Field(interior);
        fields.Field(dimension);

        // Runtime colour so setColor is not construction-only. Same two
        // bytes as OnSerializeConstruction; a late joiner sees both.
        fields.Field(primaryColour);
        fields.Field(secondaryColour);
        fields.Field(paintjob);

        for (auto &part : upgrades) {
            fields.Field(part);
        }

        for (auto &colour : colours) {
            fields.Field(colour);
        }

        fields.Field(overrideLights);
        fields.Field(locked);

        fields.Field(plateText);
        fields.Field(dirtLevel);
        fields.Field(taxiLight);
        fields.Field(headlightColor);
        fields.Field(frozen);
        fields.Field(damageProof);

        fields.Field(collisions);
        fields.Field(landingGearDown);

        fields.Field(alpha);

        fields.Field(engineBroken);
        fields.Field(wheelsCanBurst);
        fields.Field(canBeDamaged);
        fields.Field(nitroCount);
        fields.Field(variant);
        fields.Field(variant2);
    }

    void VehicleEntity::OnStateForced() {
        if (gOnVehicleForced) {
            gOnVehicleForced(this);
        }
    }
} // namespace VantaMP::Shared
