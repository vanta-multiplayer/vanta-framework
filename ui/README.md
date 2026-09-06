# Vanta interface

Solid source, Vite config and the HTML shell. The page talks to the client
through `src/lib/vanta.ts` (`window.vanta`).

```
npm install
npm run build
```

The client embeds the result at `http://vanta/ui.html`. CEF injects
`window.vanta`; the page library is `src/lib/vanta.ts`.

See [`docs/ui.md`](../docs/ui.md).
