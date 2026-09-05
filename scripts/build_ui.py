#!/usr/bin/env python3
"""Take the interface's built HTML and make it work against this host.

The page is a build artefact of the previous client's UI project, which is where
it should stay: it is a Solid app with its own toolchain, and rebuilding it here
would mean owning that toolchain too. What it expects, though, is the bridge the
OLD client injected - a `VantaNui` global with emit/on/invoke on it.

This host offers something different. The framework's CEF installs
`callEvent(name, payload)` into every page before its scripts run, and events go
the other way through ExecuteJavaScript. So the page is not modified; a shim is
prepended that implements `VantaNui` in terms of `callEvent`.

WHY PREPENDED RATHER THAN EVALUATED AFTER LOAD. `@vanta/nui` throws the moment
it is imported if the global is absent, and imports run while the page loads -
long before a host could call ExecuteJavaScript on it. Injecting afterwards
produces exactly what was seen: a page that renders, and every call into it
failing with "only available inside a Vanta NUI page".

    python scripts/build_ui.py <source index.html> -o data/ui/index.html
"""

import argparse
import sys
from pathlib import Path

# Implements what @vanta/nui expects, on top of what the framework provides.
#
# Both names are set: the package reads `VantaNui`, while the page's own bridge
# checks `__vantaNui` to decide whether it is running in a game at all. Missing
# either one leaves the interface half-working in a way that looks like the
# other half is broken.
SHIM = """<script>
(function () {
    if (typeof window.VantaNui !== "undefined") { return; }

    var listeners = {};
    var pending = {};
    var nextId = 1;

    function send(event, payload) {
        if (typeof window.callEvent !== "function") { return; }
        window.callEvent(event, JSON.stringify(payload === undefined ? {} : payload));
    }

    var api = {
        emit: function (event, payload) {
            send(event, payload);
        },

        on: function (event, handler) {
            (listeners[event] = listeners[event] || []).push(handler);
            return function () {
                listeners[event] = (listeners[event] || []).filter(function (h) { return h !== handler; });
            };
        },

        invoke: function (event, payload, timeoutMs) {
            var id = nextId++;

            return new Promise(function (resolve, reject) {
                var timer = setTimeout(function () {
                    delete pending[id];
                    reject(new Error(event + " timed out"));
                }, timeoutMs || 5000);

                pending[id] = function (result) {
                    clearTimeout(timer);
                    delete pending[id];
                    resolve(result);
                };

                send(event, { __id: id, payload: payload === undefined ? {} : payload });
            });
        },

        setFocus: function () {
            // The host decides focus: while the menu is up it is the only thing
            // on screen, and the game is not reading input through the window.
        }
    };

    window.VantaNui = api;
    window.__vantaNui = api;

    // How the host pushes an event in.
    window.__vantaDispatch = function (event, payloadJson) {
        var parsed;
        try { parsed = JSON.parse(payloadJson); } catch (e) { parsed = {}; }
        (listeners[event] || []).forEach(function (handler) {
            try { handler(parsed); } catch (e) { console.error(event, e); }
        });
    };

    // And how it answers an invoke.
    window.__vantaResolve = function (id, resultJson) {
        var resolve = pending[id];
        if (!resolve) { return; }
        var parsed;
        try { parsed = JSON.parse(resultJson); } catch (e) { parsed = undefined; }
        resolve(parsed);
    };
})();
</script>"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", type=Path, help="the UI project's built index.html")
    parser.add_argument("-o", "--output", type=Path, required=True)
    args = parser.parse_args()

    if not args.source.exists():
        print(f"error: {args.source} does not exist", file=sys.stderr)
        return 2

    html = args.source.read_text(encoding="utf-8")

    marker = "<head>"

    if marker not in html:
        print("error: no <head> to inject the bridge into", file=sys.stderr)
        return 1

    # First thing in the head, so it is defined before any module is evaluated.
    html = html.replace(marker, marker + "\n" + SHIM, 1)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(html, encoding="utf-8")

    print(f"wrote {args.output} ({len(html):,} bytes, bridge prepended)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
