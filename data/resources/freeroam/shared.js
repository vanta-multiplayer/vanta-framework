// Shared constants for freeroam (loaded on both sides via sharedScripts).
// Spawn matches shared/world/spawn_points.h — keep them in sync.

const Freeroam = {
    spawn: { x: 2899.41, y: -1672.64, z: 12.89, heading: 0 },

    // Dock row next to the pier (not in the water).
    dock: [
        { model: 560, name: 'Sultan', x: 2865.93, y: -1665.88, z: 10.88, heading: 0 },
        { model: 411, name: 'Infernus', x: 2861.5, y: -1665.88, z: 10.88, heading: 0 },
        { model: 522, name: 'NRG-500', x: 2857.0, y: -1665.88, z: 10.88, heading: 0 },
        { model: 487, name: 'Maverick', x: 2850.0, y: -1675.0, z: 11.2, heading: 1.57 },
    ],

    // GTA SA eWeaponType ids (must exist in the retail weapon catalog).
    weapons: {
        knife: 4,
        pistol: 22,
        deagle: 24,
        shotgun: 25,
        mp5: 29,
        ak47: 30,
        m4: 31,
        sniper: 34,
        grenade: 16,
    },

    skins: {
        cj: 0,
        truth: 1,
        sweet: 270,
        smoke: 269,
        ryder: 271,
        ballas: 102,
    },

    help: [
        '/help — this list',
        '/veh [model] — spawn a vehicle (default Sultan 560)',
        '/gun [name] — give a weapon (pistol, deagle, m4, …)',
        '/weapons — starter kit',
        '/heal — full health + armour',
        '/repair — fix occupied vehicle',
        '/skin [name|id] — change ped model',
        '/pos — print your position',
        '/tp x y z — teleport',
        '/jetpack — toggle jetpack',
        '/weather [id] — request weather (0–45)',
        '/time h m — request world time',
        'F4 spawn vehicle · F5 weapons · F6 heal',
    ],
};

if (typeof globalThis !== 'undefined') {
    globalThis.Freeroam = Freeroam;
}
