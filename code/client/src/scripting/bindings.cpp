#include "scripting/bindings.h"

#include "scripting/player.h"
#include "scripting/vehicle.h"

#include <scripting/engine.h>

namespace VantaMP::Scripting {
    void Register(Framework::Scripting::Engine *engine) {
        if (!engine) {
            return;
        }

        v8::Isolate *isolate = engine->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Context::Scope contextScope(engine->GetContext());

        auto global = engine->GetContext()->Global();

        Player::Register(isolate, global);
        Vehicle::Register(isolate, global);
    }
} // namespace VantaMP::Scripting
