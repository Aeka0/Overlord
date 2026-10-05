# Vehicle module

`policy.hpp`, `steering.hpp` and `fire_intent.hpp` describe copied-input decisions.
`runtime.*` applies vehicle command adaptation. `native.cpp` owns verified native
hooks and script contracts. `presentation.cpp` and `hud.*` consume copied state
for hands, model placement and feedback.

Native vehicle physics, seats, weapon identity and scripted movement remain with
H2. Rendering does not write those fields or query the script VM. Preserve native
owner-thread admission and generation checks when adding a vehicle adapter.
