# VantaMP docs

These notes are for the next person who has to change how players walk, how
cars move, or how a new GTA call gets into the client. The code comments are
still the source of truth for *why a line exists*; this folder is the map.

| Page | Start here when you need to… |
|------|------------------------------|
| [Architecture](architecture.md) | Understand the layers, who owns what, and which channel a fact rides |
| [Player sync](player-sync.md) | Change how remotes walk, aim, shoot, duck or swim |
| [Vehicles](vehicles.md) | Change enter / exit, jacking, doors, steering or car motion |
| [Voice](voice.md) | Proximity talk, devices, PTT, mic test |
| [SDK and patterns](sdk-and-patterns.md) | Call into GTA, or add a signature |
| [Setup](setup.md) | Clone or fork this repo on a new machine |
| [Building](building.md) | Build, run two clients, and not lock the DLL |
| [Interface](ui.md) | Solid UI, embed, console commands, `vanta://` |
| [Events](events.md) | Core <-> UI (`vanta.emit` / `vanta.on`) |

The short version of the whole project:

- Remote players are **input replay**, not animation replication.
- Vehicles are **three channels**: occupancy (reliable RPC), motion (owner
  replica), controls (the driver's pad, swapped onto the car).
- `sdk/` is a hard boundary. Game types live there. Everywhere else talks in
  slots, network ids and plain numbers.

Session notes for agents live in [`.ai/HANDOFF.md`](../.ai/HANDOFF.md). Prefer
this folder when the handoff and the code disagree — the handoff is a snapshot.

## Agent files

[`AGENTS.md`](../AGENTS.md) is the binding rule sheet. Everything else is a
pointer so the tool you opened actually loads it:

| File | Tool |
|------|------|
| [`AGENTS.md`](../AGENTS.md) | Cursor, Codex, Aider, anything that reads a root agent file |
| [`CLAUDE.md`](../CLAUDE.md) | Claude Code |
| [`GEMINI.md`](../GEMINI.md) | Gemini CLI |
| [`.github/copilot-instructions.md`](../.github/copilot-instructions.md) | GitHub Copilot |
| [`.windsurfrules`](../.windsurfrules) | Windsurf |
| [`CONVENTIONS.md`](../CONVENTIONS.md) | Aider |
| [`.cursor/rules/`](../.cursor/rules) | Cursor project rules (core + per-area) |
