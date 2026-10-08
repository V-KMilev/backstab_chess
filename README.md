# Backstab Chess

Chess in 3D for teams of any size, 1v1 to 3v3 and every mix. When a team's players want
different moves, a short mini-game decides whose is played - and the other team can join
in to sabotage it.

The chess set is Poly Haven's "Chess Set" by Riley Queen (CC0),
https://polyhaven.com/a/chess_set.


    vkm run                build, cook and play it
    vkm run --players 2    the same with a local server, to test multiplayer
    vkm edit               build it and open it in the editor
    vkm serve              build, cook and host it for other machines (no window)
    vkm package            build, cook and assemble the game a player gets
    vkm doctor             when something will not build or start

`vkm -h` lists every command, and `vkm <command> -h` explains one.

`serve` and `run --players` need a vkmSetupNetwork entry in src/module.cpp, which
says what a joining player is given. Without one they refuse to start rather
than serving a game nobody can be in.

## What is here

    project.json    what the game is called, the engine it was made for, and
                    which scene it opens
    CMakeLists.txt  finds the engine and compiles every .cpp under src/ into
                    one module - a new file needs no edit here
    src/            your gameplay code
    assets/         your art
    scenes/         your saved scenes
    .gitignore      what the tools generate: build/ bin/ cooked/ logs/ dist/

`src/module.cpp` holds the entry points a host looks for, and
`system/script/module_entry.h` in the engine says what each is for. Two are
required and are already written: `vkmModuleEngineVersion`, which stamps the
engine this was built against, and `vkmRegisterBehaviors`, which names your
behaviors. `vkmBuildScene` is optional - it builds a world in code, and a
project that authors scenes in the editor sets `entryScene` in `project.json`
instead.

## One thing that will catch you

Forward is `-Z`, screen-right is `+X`, and up is `+Y` - glm's own convention,
which is what `glm::quatLookAt` and every `Math::` helper already assume. Get it
backwards and nothing errors; it just looks wrong, and the tell is controls that
are mirrored rather than merely rotated.

Take the direction from `Math::computeForward` rather than writing a sign
yourself.
