# Vanta interface

Solid source, Vite config and the HTML shell. Tokens and logo match the
public site. The page talks to the client through `src/lib/vanta.ts`
(`window.vanta`). Console commands run in C++; `src/lib/commands.ts` is
only suggestions.

```
npm install
npm run build
```

The client embeds the result at `http://vanta/ui.html`. CEF injects
`window.vanta`. Native `<select>` popups do not work here — use
`Dropdown`.

See [`docs/ui.md`](../docs/ui.md) and [`docs/events.md`](../docs/events.md).
