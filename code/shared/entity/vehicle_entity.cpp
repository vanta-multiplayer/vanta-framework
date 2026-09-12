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
    }

    void VehicleEntity::SerializeFields(Replication::FieldSerializer &fields) {
        // ORDER IS THE WIRE FORMAT. Append only.
        fields.Field(angularVelocity);
        fields.Field(health);
        fields.Field(engineOn);
        fields.Field(sirenOn);
        fields.Field(steer);
        fields.Field(accelerator);
        fields.Field(brake);
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

        fields.Field(lightsOn);
        fields.Field(alpha);
        fields.Field(onGround);

        fields.Field(engineBroken);
        fields.Field(wheelsCanBurst);
        fields.Field(canBeDamaged);
        fields.Field(nitroCount);
        fields.Field(variant);
        fields.Field(variant2);

        // APPENDED. GTA's handbrake flag, which a remote bike cannot derive
        // for itself because its ProcessControlInputs never runs. Must stay
        // last: order is the wire format.
        fields.Field(handbrake);

        // APPENDED. Signed pedals, so reverse survives the wire - the
        // unsigned `accelerator`/`brake` above clamp at zero. Must stay last:
        // order is the wire format.
        fields.Field(gasPedal);
        fields.Field(brakePedal);
    }

    void VehicleEntity::OnStateForced() {
        if (gOnVehicleForced) {
            gOnVehicleForced(this);
        }
    }
} // namespace VantaMP::Shared
