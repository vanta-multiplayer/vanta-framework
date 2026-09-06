#pragma once

namespace VantaMP::SDK {
    // Getting models into memory.
    //
    // GTA streams: the world holds far more models than fit, so a model is only
    // resident while something near the camera needs it. ANYTHING THAT ASKS THE
    // GAME TO USE A MODEL HAS TO GET IT LOADED FIRST, because the game does not
    // check. Handing a ped a weapon whose model is absent records the weapon
    // happily and then faults later, when it goes to draw an object that was
    // never there - so the crash lands nowhere near the code that caused it.
    //
    // This is a whole subsystem the client will keep needing: weapons now, and
    // ped skins and vehicles as they arrive.

    // Whether a model is resident right now.
    //
    // Worth asking before either call below, because both are far more
    // expensive than the check, and the common case is that the model is
    // already there.
    [[nodiscard]] bool IsModelLoaded(int modelId);

    // Queues a model, WITHOUT WAITING. Returns once the streamer has been told,
    // not once it has finished, so a model asked for this way is not usable yet.
    void RequestModel(int modelId);

    // Blocks until everything queued has been loaded.
    //
    // A STALL, AND MEANT AS ONE. It reads off disc on the calling thread, which
    // is a visible hitch if it happens during play - the point is that it is
    // over when it returns. Use it when the next line cannot cope with the model
    // being absent, and prefer requesting early otherwise.
    void LoadAllRequestedModels();

    // Requests a model and waits for it, unless it is already loaded.
    //
    // The convenience of the three above, which is what most callers want:
    // "make sure this is usable before I continue".
    void LoadModelNow(int modelId);

    // Which model a weapon is held as, or 0 if the type has none - which is a
    // real answer for unarmed, and for anything the game does not recognise.
    [[nodiscard]] int WeaponModelId(unsigned int weaponType);
} // namespace VantaMP::SDK
