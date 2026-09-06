/* @refresh reload */
import { render } from "solid-js/web";
import { App } from "./components/App";

// Fonts before the stylesheet, so their @font-face rules are registered by the
// time anything references the families.
//
// Bundled rather than fetched: the interface loads from disk over file:// in a
// game that may have no network, so a webfont request would simply fail. The
// single-file build inlines these as data URIs. Only the weights actually used
// are imported - each one costs real bytes in the final HTML.
import "@fontsource/manrope/400.css";
import "@fontsource/manrope/500.css";
import "@fontsource/manrope/600.css";
import "@fontsource/dm-mono/400.css";
import "@fontsource/dm-mono/500.css";

// Tailwind first, as plain CSS so its Vite plugin sees the directive and scans
// the components. Going through Sass hides it: Sass resolves @use itself and
// hands Vite a finished stylesheet, so no utilities are ever generated.
import "./tailwind.css";

import "./styles.scss";

const root = document.getElementById("root");

if (root === null) {
    throw new Error("No #root element; index.html is malformed.");
}

render(() => <App />, root);
