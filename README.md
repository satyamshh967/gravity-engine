# 🌌 2D N-Body Gravity Engine (C++20 & Raylib)

> **High-performance, modular 2D N-Body celestial mechanics and gravity simulation engine built with modern C++20 and Raylib.**

[![C++20](https://img.shields.io/badge/Language-C%2B%2B20-00599C?style=flat-square&logo=cplusplus&logoColor=white)](https://en.cppreference.com/w/cpp/20)
[![Raylib](https://img.shields.io/badge/Graphics-Raylib_5.0-white?style=flat-square&logo=raylib&logoColor=black)](https://www.raylib.com/)
[![Physics](https://img.shields.io/badge/Integrator-Velocity_Verlet-7928CA?style=flat-square)]()
[![CMake](https://img.shields.io/badge/Build-CMake_%2B_Ninja-064F8C?style=flat-square&logo=cmake&logoColor=white)](https://cmake.org/)
[![License](https://img.shields.io/badge/License-MIT-blue?style=flat-square)](./LICENSE)

---

## 📖 Overview

A high-performance, modular 2D N-Body celestial mechanics and gravity simulation engine built with modern **C++ (C++20)** and **Raylib**. Features symplectic numerical integrators, momentum-conserving inelastic and elastic collision physics, orbital trail rendering, interactive slingshot trajectory prediction, an object inspector with real-time property editing, and a custom launcher settings menu.

---

## 🚀 Key Features

### 1. Physics Solvers & Symplectic Integrators
- **Velocity Verlet:** Symplectic 2nd-order time-reversible integrator preserving mechanical energy and orbital phase space over prolonged simulations.
- **Symplectic Euler:** 1st-order symplectic integrator available for live comparison.
- **Newtonian Gravity with Plummer Softening:**
  ```math
  F = \frac{G \cdot m_1 \cdot m_2}{r^2 + \epsilon^2}
  ```
  Prevents numerical singularity explosions during hyper-close encounters.
- **Decoupled Fixed-Timestep Simulation:** Fixed physics step ($\Delta t = 0.002\,\text{s}$) with an accumulator sub-stepping loop decoupled from display refresh rates, supporting variable time-dilation ($0.25\times$ to $10.0\times$).

### 2. Collision Dynamics & Destruction Physics
- **Cataclysmic Destruction & Shattering:** High-energy impacts trigger explosive fragmentation where celestial bodies break apart into dynamic child debris fragments while strictly conserving linear momentum and mass.
- **Destruction Particle System:** Over 150+ glowing incandescent embers, molten sparks, and expanding shockwave rings with dynamic camera shake upon collision or detonation.
- **Inelastic Merging:** Total linear momentum conservation ($m_1 \vec{v}_1 + m_2 \vec{v}_2 = (m_1 + m_2)\vec{v}_{new}$), 2D disk area conservation ($R = \sqrt{r_1^2 + r_2^2}$), and collision sparks.
- **Elastic Bouncing:** Momentum and kinetic energy preserving impulse resolution with contact sparks.
- **Ghost Mode:** Uncollided pass-through for studying gravitational lensing and orbital flybys.

### 3. Object Inspector & Real-Time Property Modifiers
- **Full-Height Docked Sidebar:** Enlarged 480px–560px full-height sidebar with spacious card layouts utilizing the entire screen height.
- **Hide / Show Option:** Collapse the sidebar anytime with the **`[HIDE >>]`** button or **`H`** key; a glowing **`[<< SHOW SIDEBAR]`** pill tab remains on the right screen edge for instant 1-click re-opening.
- **Target Locking:** Click any body in space to inspect and lock onto it with an animated targeting reticle.
- **Live Diagnostics:** Surface gravity $g = \frac{G m}{r^2 + \epsilon^2}$, escape velocity $v_{esc} = \sqrt{\frac{2 G m}{r}}$, net gravitational pull $|\vec{a}|$, instantaneous speed $|\vec{v}|$, density $\rho = \frac{m}{\pi r^2}$, coordinates $(X, Y)$, and velocity $(V_x, V_y)$.
- **Live Adjustments:** Real-time modification of Mass (`-50%` to `+50%`, `x10`, `/10`), Radius, and Density with stepper buttons.
- **Action Tools:** Lock Camera Track, Toggle Static Anchor, Halt Velocity ($v=0$), Auto Circularize Orbit around dominant attractor, 8-color palette picker, Clear Trail, and Cataclysmic Object Explosion (`K`).

### 4. Custom Launcher & Spawner Settings Tab (`TAB`)
- **Presets:** Quickly switch launch payload between *Asteroid*, *Moon*, *Terrestrial Planet*, *Gas Giant*, *Star*, or *Singularity (Black Hole)*.
- **Spawn Modes:**
  - *Slingshot (Drag):* Pull back to set direction and velocity with real-time trajectory prediction.
  - *Auto Circular Orbit:* Click anywhere in space to automatically calculate and inject the body into a stable circular orbit around the nearest massive body!
  - *Stationary:* Place stationary floating masses or anchored static stars.
  - *Customizable Launch Properties:* Modify launch mass, radius, density, color palette, anchor state, and trajectory prediction steps.

### 5. Preconfigured Astronomical Presets
1. **Solar System:** Central star, Mercury, Venus, Earth + Moon system, Mars + Phobos, Jupiter + Galilean moons (Io & Europa), Saturn, and a high-eccentricity Halley-like comet.
2. **Figure-8 Three-Body Orbit:** The celebrated Chenciner & Montgomery choreographed stable solution.
3. **Binary Star System & Protoplanetary Disk:** Two massive co-orbiting stars surrounded by an accretion disk of 220+ debris particles.
4. **Earth Gravity Laboratory Room:** A physical containment chamber on Earth subject to a uniform downward acceleration field $\vec{g} = (0, 9.81\ \text{m/s}^2)$. Demonstrates Galileo's principle where all objects fall at the exact same rate independent of mass. Objects bounce elastically with restitution, and when dropped from high elevation exceeding the critical shatter threshold, they explode violently into debris fragments, fire sparks, and shockwaves upon floor impact.

---

## 📁 Project Structure

```text
├── CMakeLists.txt              # Build configuration (local raylib or FetchContent)
├── Launch Gravity Engine.bat   # One-click Windows launcher
├── include/
│   ├── Vec2.hpp                # Lightweight 2D double-precision vector math
│   ├── Body.hpp                # Celestial body struct & trail history
│   ├── LaunchSettings.hpp      # Spawner configuration, presets & modes
│   ├── GravityEngine.hpp       # Physics solver, celestial presets & querying
│   └── Renderer.hpp            # Raylib camera, trails, bodies, HUD & UI panels
├── src/
│   ├── GravityEngine.cpp       # Verlet/Euler integration, collision & energy math
│   ├── Renderer.cpp            # Visual rendering & interactive GUI panels
│   └── main.cpp                # App loop, accumulator, input routing & headless test
└── README.md
```

---

## 🎮 Controls & Keybindings

| Key / Input | Action |
|---|---|
| `Left Click Body` | Select and open **Object Inspector** in sidebar |
| `H` | Hide / Show **Right Sidebar** |
| `TAB` | Toggle between **Inspector** and **Spawner** tabs |
| `ESC` | Deselect body or hide sidebar |
| `SPACE` | Pause / Resume simulation |
| `1` | Load **Solar System** Preset |
| `2` | Load **Figure-8 Three-Body** Preset |
| `3` | Load **Binary Star + Protoplanetary Disk** Preset |
| `4` | Load **Earth Room Laboratory** (Uniform Downward $g$, Galileo Fall & Shatter) |
| `I` | Toggle Integrator (**Velocity Verlet** $\leftrightarrow$ **Symplectic Euler**) |
| `C` | Cycle Collision Mode (**Inelastic Merge** $\rightarrow$ **Destruction Shatter** $\rightarrow$ **Elastic** $\rightarrow$ **Ghost**) |
| `K` | Cataclysmic Destruction of selected body (Explosion + Particles + Shockwave) |
| `T` | Toggle Orbital Trails |
| `V` | Toggle Velocity Vectors |
| `G` | Toggle Coordinate Grid & Distance Rings |
| `F` | Toggle Follow Heaviest Body |
| `R` | Reset Camera Position and Zoom |
| `X` | Clear all bodies from space |
| `[` / `]` or `,` / `.` | Adjust Time Scale ($0.25\times$ – $10\times$) |
| `+` / `-` | Increase / Decrease Spawning Mass |
| `Left Click + Drag` | Slingshot spawn a new body (with trajectory preview) |
| `Right Click + Drag` | Pan camera across space |
| `WASD` or Arrow Keys | Pan camera |
| `Mouse Wheel` | Smooth zoom towards mouse cursor |

---

## 🛠️ Building and Running

### Prerequisites
- Modern C++ compiler supporting **C++20** (GCC 11+, Clang 13+, or MSVC 2022)
- **CMake** (v3.20+) and **Ninja** / Make

### Build Commands
```bash
# Generate build files
cmake -B build -G "Ninja"

# Compile executable
cmake --build build
```

### Running the Application
```bash
# Graphical Simulation
./build/GravityEngine.exe

# Headless Unit Test (Verifies energy conservation < 0.01% drift over 1,000 steps)
./build/GravityEngine.exe --test
```

---

## 👤 Author
- **Satyam Sharma** - [@satyamshh967](https://github.com/satyamshh967)
