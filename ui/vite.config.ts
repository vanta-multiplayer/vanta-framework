import { defineConfig } from "vite";
import solid from "vite-plugin-solid";
import { viteSingleFile } from "vite-plugin-singlefile";
import tailwindcss from "@tailwindcss/vite";

/**
 * Builds the interface into ONE self-contained index.html.
 *
 * WHY A SINGLE FILE
 * CEF loads this from disk through a file:// URL, not over HTTP. Under file://
 * there is no origin, so a tree of separate scripts and stylesheets has to
 * resolve every relative path correctly and is easy to break by moving the
 * folder. One file has nothing to resolve: the client points CEF at index.html
 * and everything is already there.
 *
 * It also makes shipping trivial - the packaging script copies a single file
 * rather than a directory whose contents it would otherwise have to know.
 */
export default defineConfig({
    plugins: [
        solid(),

        // Tailwind v4 is configured in CSS rather than a config file - the
        // theme lives in styles.scss beside the styles that use it.
        tailwindcss(),

        // Inlines every script, style and asset into the HTML. Must come after
        // solid(), which is what produces the bundle this then folds together.
        viteSingleFile(),
    ],

    base: "./",

    build: {
        outDir: "dist",
        emptyOutDir: true,

        // No separate asset files at all: anything not inlined would defeat the
        // point and leave a file the client does not know to copy.
        assetsInlineLimit: Number.MAX_SAFE_INTEGER,
        cssCodeSplit: false,

        rollupOptions: {
            output: {
                // A single chunk, so there is nothing to import at runtime.
                inlineDynamicImports: true,
            },
        },
    },
});
