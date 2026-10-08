# Backstab Chess

Chess in 3D at a lantern-lit table on a sea of wrecked chess sets, for two to eight
players in two teams. Pieces belong to players, teammates fight over them, every
contested move is a duel, and a king has three lives before it falls.

The chess set is Poly Haven's "Chess Set" by Riley Queen (CC0),
https://polyhaven.com/a/chess_set.

## Build and play

You need a vkmEngine checkout **on `vkm/dev`**. The released 1.0.3 SDK is missing the
command payload that online play sends its messages in. Put both repos side by side:

```sh
git clone --recursive git@github.com:V-KMilev/vkmEngine.git
git clone git@github.com:V-KMilev/backstab_chess.git

cd vkmEngine
git checkout vkm/dev
git submodule update --init --recursive
cmake -B build -G Ninja           # the first configure fetches the pinned compiler
cmake --build build -j4
cd ..
```

Then, from the folder holding both:

```sh
vkmEngine/tools/vkm run backstab_chess               # play
vkmEngine/tools/vkm run backstab_chess --players 2   # a local server and two windows
vkmEngine/tools/vkm serve backstab_chess             # a server only, for other machines
```

On Windows (MSYS2 UCRT64 or Git Bash) the commands are the same; from `cmd.exe` or
PowerShell use `vkmEngine\tools\vkm.cmd`. `vkm run` builds the game and cooks its art
first, so it is the one command to repeat.

## Playing online

From the main menu choose **Play online**:

- **Host** starts a server for this game on your machine (the port defaults to 27750)
  and seats you at it. Others join with your address. Across the internet, that port
  (UDP) has to be open or forwarded to you.
- **Join** takes `host` or `host:port`.

In the lobby each player sits on a team, and **Switch** moves you to the other one.
The host chooses the turn time and starts once both teams have a player. When the
match ends, the host takes everyone back to the lobby for another. A table that is
full (eight seats) or in the middle of a match turns joiners away.

To try it alone, `vkm run backstab_chess --players 2` opens two windows already
seated at one server. Or start the game twice: host in one window and join
`127.0.0.1` in the other.

**Practice** plays offline against bots, which are there for testing. Everyone plays
from their own seat.

## How it plays

- Click one of your pieces, then a marked square. A blue mark is a plain move; a red
  mark is a duel.
- **Duels.** Moving a teammate's piece is a duel with that teammate. Taking a piece
  someone owns is a duel with its owner, and if you lose, they move out of turn. Stop
  your needle (Space or click) nearer the gold middle than the other player does to
  win. A tie goes to the defender.
- **Kings.** A check that is not mate opens a strike on the king. Each strike won costs
  it a life and is worth points, and whoever takes its last life takes the lot.
  Checkmate still ends the game.
- **The day and night clock.** White moves by day and black by night. The sun-and-moon
  dial is the turn's time, duel included.
- **Controls.** Right-drag to look, WASD to fly, Space and Shift for up and down, and
  F to go back to your seat. Esc pauses. Every key can be rebound in Settings >
  Controls.

## Making it yours

**Customize** designs what your pieces are made of (wood, stone, metal, glass, gem or
neon), their tint, shine and glow. It also sets how your takes fly and how a claimed
piece turns into your skin, all against a live preview. **Settings** covers graphics
(a quality preset, or each effect on its own), the game and the controls.

Both are saved in `~/.config/backstab_chess` (`%APPDATA%\BackstabChess` on Windows).

## The code

| Path | What it holds |
| --- | --- |
| `src/chess/` | The rules: chess itself (checked against perft counts), and the match with its owners, duels, strikes and points. |
| `src/chess_game.*` | The table in play: pieces, hints, takes, claims, duels, the HUD and the sun. |
| `src/menu.*` | Every screen. `ui_kit.*` holds its widgets. |
| `src/net_link.*`, `src/net_state.*` | Online play. The server runs the match and replicates the seats and every action, and clients replay the actions through the same rules. |
| `src/scenery.*`, `src/sea.*`, `src/textures.*`, `src/chess_look.*` | The world and its materials. |
| `tests/` | The rules, the take and claim animations, and the network state. They build with the game; run them from `build/development/`. |
