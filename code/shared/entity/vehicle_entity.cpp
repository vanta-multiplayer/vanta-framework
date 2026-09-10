#include "vehicle_entity.h"
#include "state_forced.h"

namespace VantaMP::Shared {
    namespace Replication = Framework::Networking::Replication;

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
    }

    void VehicleEntity::OnStateForced() {
        if (gOnVehicleForced) {
            gOnVehicleForced(this);
        }
    }
} // namespace VantaMP::Shared
