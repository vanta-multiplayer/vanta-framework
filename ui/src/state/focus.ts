import { emit } from "../lib/vanta";

/**
 * Who currently needs input, across the whole interface.
 *
 * WHY THIS IS CENTRAL RATHER THAN PER-COMPONENT
 * Focus is a single piece of state shared by every surface, but each surface
 * only knows about itself. When the menu and the console each called setFocus
 * directly, closing the console told the game "no keyboard, no mouse" - which
 * also stripped the menu's focus even though the menu was still open. The last
 * component to speak won, which is not what either of them meant.
 *
 * Each surface declares what IT needs and this works out the total: the
 * keyboard is captured if anything wants it, and so is the mouse. Nothing is
 * released until every surface has let go.
 */

interface Claim {
    readonly keyboard: boolean;
    readonly mouse: boolean;
}

const claims = new Map<string, Claim>();

let lastKeyboard = false;
let lastMouse = false;

/**
 * Declares what a surface needs, replacing its previous claim.
 *
 * @param owner Identifies the surface. Use a stable name.
 */
export function claimFocus(owner: string, keyboard: boolean, mouse: boolean): void {
    if (!keyboard && !mouse) {
        claims.delete(owner);
    } else {
        claims.set(owner, { keyboard, mouse });
    }

    let wantsKeyboard = false;
    let wantsMouse = false;

    for (const claim of claims.values()) {
        wantsKeyboard ||= claim.keyboard;
        wantsMouse ||= claim.mouse;
    }

    // Only on a change. Every call crosses into the game, takes the cursor and
    // clears the pad, so repeating the current state each frame would be
    // needless work - and would fight anything else touching the cursor.
    if (wantsKeyboard === lastKeyboard && wantsMouse === lastMouse) {
        return;
    }

    lastKeyboard = wantsKeyboard;
    lastMouse = wantsMouse;

    // The client uses this for the cursor. Menu mouse = F1 menu is up.
    emit("ui:toggle", wantsMouse);
}

/** Drops a surface's claim entirely. */
/**
 * True when some OTHER surface holds the keyboard.
 *
 * A hotkey must not fire while the player is typing somewhere else. The console
 * and the chat box both listen on `window`, so without this, Enter in the
 * console also opened chat - each surface saw a key that was meant for the
 * other.
 */
export function keyboardHeldByOther(owner: string): boolean {
    for (const [claimant, claim] of claims) {
        if (claimant !== owner && claim.keyboard) {
            return true;
        }
    }

    return false;
}

export function releaseFocus(owner: string): void {
    claimFocus(owner, false, false);
}
