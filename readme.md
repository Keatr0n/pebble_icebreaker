# pebble_icebreaker

A cyberpunk lockpicking game for the Pebble Time 2 (emery). Six locks, each testing a different skill, played on their own or chained together into a heist.

## The locks

| Lock | Skill | How it plays |
| --- | --- | --- |
| ROTOR | Timing | A pick sweeps around each tumbler ring. Press Select while it's in the lit gate. Rings speed up, gates shrink, and from level 4 the gates drift. Three slips and you fail. |
| BREACH | Logic | Cyberpunk 2077's breach protocol. Picks alternate between the lit row and the lit column, starting on the top row. Get UNLOCK's codes into the buffer in order before it fills or the trace runs out. DATAMINE and ICEPICK are bonus points. Hold Down to cash out early once UNLOCK is in. |
| ECHO | Memory | Three nodes, one per button, fire a pattern. Play it back. Each of the three rounds adds a step, and pink REVERSE rounds (level 3 and up) are entered backwards. |
| CIPHER | Deduction | Mastermind, with no symbol repeated in the key. Up/Down change the symbol, Select moves to the next slot and sends from SEND (or hold Select), and Back goes to the previous slot. A cyan pip means right symbol, right slot. A yellow pip means right symbol, wrong slot. |
| BITLOCK | Mental maths | Flip bits until the register equals the target. Targets start as decimal, then become hex, then sums and differences. The place values stop being shown after level 2, and from level 4 the register starts scrambled. |
| FIREWALL | Reflex | Survive 20 seconds in a four-lane data stream. Up/Down change lane, and dodge the red ICE. Every 3 shards gives you a pulse charge, and Select fires it to blow away the wall ahead. |

## Modes

- **Training**: one lock, a level harder each time you crack it, until one holds. Your score is the whole run's total.
- **Heist**: a random lock on every floor (each kind once per six floors), getting harder every two floors. A failed lock costs one of three integrity and you carry on. Cracked floors pay a floor bonus.

Back pauses a lock (in CIPHER, only from the first slot). The board is hidden while paused, so a pause doesn't buy thinking time. Backing out of a briefing mid-run banks the score so far, and so does leaving the app.

## Scores

The menu shows the best score for every lock and for the heist, the deepest heist floor, how many locks you've cracked in total, and a rank earned from your best heist (SCRIPT KIDDIE, GLITCHER, NETRUNNER, ICEBREAKER, LEGEND). A training briefing shows that lock's best score and the highest level you've reached on it. Haptics can be switched off from the bottom of the menu.

## Building

```sh
nix develop
pebble build
pebble install --emulator emery   # or --phone <ip>
```

The dev shell puts SDL3 on `LD_LIBRARY_PATH` because the emulator's SDL2 is sdl2-compat, which loads SDL3 at runtime.
