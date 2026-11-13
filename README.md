# 🌌 2D N-Body Gravity Engine (C++20 & Raylib)

> **An interactive 2D gravity sandbox: launch planets with a slingshot, watch orbits, merge or shatter worlds, and drop anvils and apples in an Earth gravity lab.**

[![C++20](https://img.shields.io/badge/Language-C%2B%2B20-00599C?style=flat-square&logo=cplusplus&logoColor=white)](https://en.cppreference.com/w/cpp/20)
[![Raylib](https://img.shields.io/badge/Graphics-Raylib_5.5-white?style=flat-square&logo=raylib&logoColor=black)](https://www.raylib.com/)
[![Physics](https://img.shields.io/badge/Integrator-Velocity_Verlet-7928CA?style=flat-square)]()
[![CMake](https://img.shields.io/badge/Build-CMake_%2B_Ninja-064F8C?style=flat-square&logo=cmake&logoColor=white)](https://cmake.org/)
[![Tests](https://img.shields.io/badge/Tests-CTest-2EA44F?style=flat-square)](./src/PhysicsTests.cpp)
[![License](https://img.shields.io/badge/License-MIT-blue?style=flat-square)](./LICENSE)

---

## 📖 Overview

A modular 2D N-body gravity simulator built with modern **C++20** and **Raylib**. The physics core is a standalone library with no graphics dependency and its own test suite. It covers symplectic integrators, collisions that conserve mass and momentum (merge, shatter, bounce), and an Earth gravity lab with a real metric scale. The app adds an animated, fully clickable interface on top.

---

## 🚀 Key Features

### 1. Physics Solvers & Symplectic Integrators
- **Velocity Verlet** (2nd order, time-reversible, energy-stable, the default) and **Symplectic Euler** (1st order, for live comparison).
- **Newtonian gravity with Plummer softening:**
  ```math
  \vec{a} = \frac{G\, M\, \vec{r}}{(r^2 + \epsilon^2)^{3/2}}
  ```
- **Fixed timestep** of $\Delta t = 0.002\,\text{s}$, decoupled from the frame rate, with a speed range of $0.1\times$ to $20\times$. A per-frame CPU budget prevents the "spiral of death", and the HUD shows the *real* speed if the CPU can't keep up.
- **Energy drift readout:** the baseline resets automatically on every discontinuous change (spawn, merge, edit), so the number always means something.

### 2. Collision Dynamics & Destruction Physics
- **Merge:** conserves momentum, mass, centre of mass and disk area ($R = \sqrt{r_1^2 + r_2^2}$).
- **Shatter:** if the impact energy exceeds the pair's gravitational binding energy ($G M^2 / R$), both bodies break into fragments. Fragments conserve mass, momentum and centre of mass **exactly**. Gentler impacts merge.
- **Bounce:** elastic impulses with adjustable restitution.
- **Ghost:** bodies pass through each other.
- **Explosions (`K`):** shatter any body into debris ejected faster than its escape velocity.
- **Sweep-and-prune** collision detection: only nearby pairs are tested.
- Sparks, shockwave rings, camera shake and screen flash, kept separate from the physics.

### 3. Object Inspector, Spawner & World Tabs
- **Sidebar** (slides in and out, scrollable, three tabs):
  - **Inspect:** mass, radius and density sliders; live speed, acceleration, surface gravity, escape velocity, orbital period and eccentricity; an orbit status line; and Halt / Reverse / Boost / Circularize / Anchor / Explode / Delete.
  - **Spawn:** payload presets with a live preview, spawn mode (slingshot, auto orbit, stationary), sliders, colour and path-preview length.
  - **World:** scene picker, integrator and collision mode, physics constants (G, softening, restitution, shatter threshold, or the lab's g, bounciness and shatter speed), display toggles, UI scale, and reload/clear.
- **Slingshot launch** with a live N-body path preview. The path is colour-coded 🟢 bound orbit, 🟠 escape, 🔴 impact or shatter.
- **Clickable HUD** and a **time bar** with play/pause, single step, a speed slider and display toggles.
- **Feel and feedback:** toast notifications, hover cards, tooltips, animated buttons, a parallax starfield, an adaptive grid, smooth camera framing and a help overlay (`F1`).
- **Display:** the UI scales with monitor DPI, and fonts are mipmapped so they stay crisp.

### 4. Preconfigured Scenes
1. **Solar System:** a dynamic Sun, 6 planets, 4 moons and a Halley-style comet. Planets sit more than 3.5 mutual Hill radii apart and every moon orbits well inside its planet's Hill sphere, so the system is stable.
2. **Figure-8 Three-Body Orbit:** the Chenciner & Montgomery choreography, with about a 27 s period.
3. **Binary Star + Disk:** two stars with a 220-particle dust disk that starts at 3x the binary separation, where circumbinary orbits are stable.
4. **Earth Gravity Lab:** a uniform $g = 9.81\ \text{m/s}^2$ at 73.4 px per metre.
   - A 500 kg anvil and a 0.5 kg apple fall identically (Galileo).
   - Drops from above the critical height (about 4.4 m) shatter on impact.
   - g, bounciness and shatter speed are adjustable live.

---

## 📁 Project Structure

```text
├── CMakeLists.txt              # gravity_core (no graphics), GravityTests (CTest), GravityEngine app
├── Launch Gravity Engine.bat   # One-click Windows launcher (builds if needed)
├── include/
│   ├── Vec2.hpp, Rgba.hpp      # Math & colour, no raylib dependency
│   ├── Body.hpp                # Body state, trail, shared size/mass limits
│   ├── Effects.hpp             # Sparks, shockwaves, shake, flash (visual only)
│   ├── GravityEngine.hpp       # Physics engine API
│   ├── LaunchSettings.hpp      # Spawner payload presets
│   ├── PhysicsTests.hpp        # Headless test suite entry point
│   ├── Ui.hpp                  # Immediate-mode UI toolkit (buttons, chips, sliders, tooltips)
│   └── Renderer.hpp            # Camera, world drawing, HUD, sidebar
├── src/
│   ├── GravityEngine.cpp       # Integration, collisions, fragmentation, queries
│   ├── Scenes.cpp              # Preset scenes
│   ├── PhysicsTests.cpp        # Conservation & regression tests
│   ├── Ui.cpp, Renderer.cpp, Sidebar.cpp
│   └── main.cpp                # Input, fixed-step loop, event handling
├── tests/run_tests.cpp         # CTest runner
└── .github/workflows/ci.yml    # Builds and tests the physics core on Linux & Windows
```

---

## 🎮 Controls & Keybindings

| Key / Input | Action |
|---|---|
| `Left Click Body` | Select and inspect |
| `Left Click + Drag` | Slingshot launch (wheel while dragging = mass, right click = cancel) |
| `Right Click + Drag` / `WASD` / Arrows | Pan camera |
| `Mouse Wheel` | Zoom towards cursor |
| `SPACE` / `N` | Pause / step one frame |
| `[` / `]` or `,` / `.` | Slower / faster ($0.1\times$ – $20\times$) |
| `1` `2` `3` `4` / `0` | Solar / Figure-8 / Binary / Earth Lab / empty space |
| `I` | Toggle integrator (**Velocity Verlet** $\leftrightarrow$ **Symplectic Euler**) |
| `C` | Cycle collisions (**Merge** $\rightarrow$ **Shatter** $\rightarrow$ **Bounce** $\rightarrow$ **Ghost**) |
| `K` / `Delete` | Explode / delete selected body |
| `T` `V` `G` `L` | Trails / velocity vectors / grid / labels |
| `F` / `R` | Follow heaviest body / re-frame scene |
| `H` / `TAB` | Show-hide sidebar / next tab |
| `X` twice | Clear all bodies |
| `+` / `-` | Launch mass |
| `ESC` | Close help, then cancel drag, then deselect, then hide sidebar |
| `F1` or `?` | Help overlay |

---

## 🛠️ Building and Running

### Prerequisites
- Modern C++ compiler supporting **C++20** (GCC 11+, Clang 13+, or MSVC 2022)
- **CMake** (v3.20+) and **Ninja** / Make
- **Raylib 5.x** (used if installed; otherwise 5.5 is fetched automatically)

### Build Commands
```bash
# Generate build files (defaults to an optimized Release build)
cmake -B build -G "Ninja"

# Compile the app, the physics library and the tests
cmake --build build
```

### Running the Application
```bash
# Graphical simulation (or double-click "Launch Gravity Engine.bat")
./build/GravityEngine.exe
```

### Running the Tests
```bash
ctest --test-dir build --output-on-failure   # or ./build/GravityTests.exe, or ./build/GravityEngine.exe --test
```

The suite checks:
- Energy drift over a full orbit.
- Mass, momentum and centre-of-mass conservation in merges, shatters and explosions.
- Wall bounces in the room.
- Galileo free fall and floor shatter.
- Energy-baseline resets.
- Long-term stability of the solar system's moons.
- The figure-8 choreography.
- Slingshot-preview accuracy.
- Unique body ids.

To build only the physics core and tests (no raylib needed):
```bash
cmake -B build -DGRAVITY_BUILD_APP=OFF && cmake --build build && ctest --test-dir build
```

---

## 📄 License

Released under the [MIT License](./LICENSE).

## 👤 Author
- **Satyam Sharma** - [@satyamshh967](https://github.com/satyamshh967)
