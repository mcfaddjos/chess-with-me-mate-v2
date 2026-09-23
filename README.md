# ChessWithMeMate v2

A 3D chess game in C++ with legacy OpenGL and GLUT. It's a continuation of my **CS 450/550 (Computer Graphics), Oregon State University, Fall 2023** final project. The school version is preserved at [chess-with-me-mate](https://github.com/mcfaddjos/chess-with-me-mate) and tagged `v1-school-final` here.

v2 keeps the low-poly, flat-shaded, pixel-font look, but plays real chess: full rules, a chess clock, and a board you can orbit and click accurately.

<p align="center"><img src="docs/board.png" alt="The 3D chess board mid-game, viewed from above (v1 screenshot)" width="480"><br><em>v1 screenshot. v2 adds a coordinate border, move highlights, and an on-screen status line.</em></p>

## What's new in v2

- **Full rules.** Legal moves only, turn order, check, checkmate, stalemate, castling, en passant, promotion, the fifty-move rule, and insufficient-material draws. The rules live in [`chessrules.h`](chessrules.h), which has no graphics code and is checked by [perft tests](tests/perft.cpp).
- **Accurate clicking.** Clicks are cast into the scene with the same matrices used for drawing, then tested against each piece's actual triangles. A click lands on what it looks like it lands on, from any camera angle.
- **Orbit camera.** Shift+drag or the arrow keys orbit the board, the wheel zooms, and `V` flips to the other side.
- **Time-based animation.** Pieces hop to their square, knights hop higher, and captured pieces topple with the original wobble and then fade out.
- **Redraw on demand.** The game only redraws when something changes, so it sits near 0% CPU while idle, down from 100% of a core in v1.
- **Board feedback.** Highlights show the selected piece, legal moves, the last move, and a king in check. The board border has a–h and 1–8 labels, and the status line uses a pixel font.
- **Game options** (right-click → Game Options):
  - Time control: untimed, 1+0, 3+2, 5+0, 10+0, 15+10, or 30+0
  - Move hints on or off
  - Auto-flip the board to face whoever is moving
  - Always promote to a queen, or choose the piece

## Controls

| Input | Action |
| --- | --- |
| Click a piece, then a highlighted square | Move. Click the rook to castle, click the piece again to deselect. |
| Shift + drag, or arrow keys | Orbit the camera |
| Wheel, or `+` / `-` | Zoom |
| Right-click | Menu: new game, undo, game options, view, projection, debug |
| `N` / `U` | New game / undo (undo is off in timed games) |
| `P` | Pause the clock (timed games) |
| `1`–`4` | Choose a promotion piece (when "Choose" is on) |
| `V` | Flip the board |
| `L` | Point light / spotlight |
| `w` `r` `g` `b` `y` | Light color |
| `F` | Freeze animations |
| `D` | Debug view: pick ray, bounding boxes, click/move log on stderr |
| `Q` / `Esc` | Quit (`Esc` cancels a pending promotion first) |

Command line options: `-d` starts in debug mode, `-fen "<position>"` starts from a FEN position, and `-tc <n>` picks a time control (0 = untimed).

## Building, testing, and releasing

Requires Visual Studio 2022 with the C++ workload. GLEW, freeglut, and GLM are included in the repo.

```powershell
.\scripts\build.ps1                      # Release build (-Configuration Debug for Debug)
.\scripts\test.ps1                       # rules tests (perft + checkmate/stalemate/draw detection)
.\scripts\test.ps1 -UI                   # also plays scripted games in a real window (moves your mouse)
.\scripts\package.ps1 -Version 2.0.0     # build + test + zip into dist\
```

The UI tests write a log, a screenshot per case, and `summary.txt` to `tests\build\ui\`.

**Releasing:** push a version tag (`git tag v2.0.0`, then `git push origin v2.0.0`). GitHub Actions builds, tests, packages, and publishes the release zip. Every push to `main` also builds and runs the rules tests.

To play a packaged build, unzip it and run `ChessWithMeMate.exe`. Keep `pieces\` and the two DLLs next to it.

## Project layout

```
sample.cpp          The game: rendering, animation, camera, picking, input, menus, clock
chessrules.h        Chess rules: board, legal move generation, make/unmake, game status
loadobjfile.cpp     .obj model loader (course-provided, also feeds triangles to picking)
setlight.cpp        Lighting helpers (course-provided)
setmaterial.cpp     Material helpers (course-provided)
keytime.*           Keyframe interpolation (course-provided), used for the topple animation
pieces/             Chess piece models
glm/                GLM math library (vendored)
scripts/            build / test / package scripts
tests/              perft rules tests and scripted UI tests
```

## Known limitations

- No threefold-repetition draw yet.
- If a player runs out of time, they lose unless the opponent has only a king, or a king plus one bishop or knight. The official rule also calls a draw in some other rare positions.
- Picking a time control starts a new game right away.
- No computer opponent yet. That's next.

## Acknowledgments

Built on the Oregon State CS 450/550 course sample framework. Uses [GLM](https://github.com/g-truc/glm), [GLEW](https://glew.sourceforge.net/), and [freeglut](https://freeglut.sourceforge.net/).
