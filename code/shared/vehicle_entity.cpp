#include "vehicle_entity.h"

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
    }
} // namespace VantaMP::Shared
