#include "player_entity.h"

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
    }
} // namespace VantaMP::Shared
