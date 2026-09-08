# `@vantamp/types`

Ambient TypeScript types for VantaMP resource scripts (server and client).

Globals match the runtime (`Events`, `Player`, `Vehicles`, `World`, `Color`, …),
not a `Core.*` namespace. Most APIs are generated from v8pp scripting catalog
dumps. Client-only Key / Discord keep polished Framework hand-written files
under `vendor/mafiahub/` (synced from the Framework tree's `types/`).

## Use in a resource

```jsonc
// tsconfig.json (server scripts)
{
  "compilerOptions": {
    "target": "ES2020",
    "module": "ESNext",
    "strict": true,
    "noEmit": true,
    "types": ["@vantamp/types/server"]
  },
  "include": ["**/*.ts"]
}
```

Point `node_modules/@vantamp/types` at this folder (path dependency or
`npm link`), or set `typeRoots` / a path mapping to `../../types`.

Use **either** `@vantamp/types/server` **or** `@vantamp/types/client` in a
resource — never both in the same `tsconfig` (side-specific globals like
`Player` / `Events` / `Voice` differ). Client and server live in separate
folders (`types/client`, `types/server`) so the IDE does not load both into
one program when browsing this package.

Client scripts use `"types": ["@vantamp/types/client"]` — includes readonly
`Player` / `Vehicle` handles (`localPlayer`, `Player.getLocal()`, …) plus
Framework Key / Discord.

## Regenerate

1. Dump catalogs (starts the server briefly; client dump needs a game session):

   ```powershell
   scripts\dump_scripting_catalog.ps1 -Side server
   scripts\dump_scripting_catalog.ps1 -Side client   # optional; after client scripting inits
   ```

2. Emit `.d.ts` (also syncs vendor files from Framework `types/`):

   ```bat
   scripts\build.bat types
   ```

   or `python scripts\generate_scripting_types.py`

Catalog JSON is checked in under `catalog/` so a clone can regenerate without
running the game. Property-heavy Vanta types that the dump does not yet
document (`Player` / `Vehicle` fields) live in `supplements/server.json` and
are merged at generate time.
