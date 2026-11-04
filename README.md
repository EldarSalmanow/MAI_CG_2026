# 🌋 Vulkan Starter App

## Getting started

You need C++ compiler, Vulkan SDK and CMake installed before you can build this project.

This project uses C++20 standard and thus requires either of those compilers:
- GCC 10.X
- Clang 10
- Microsoft Visual Studio 2019

This is officially tested on *Windows* and *GNU/Linux platforms*, no *macOS* support yet.
If you have a working macOS solution of this code, consider submitting a PR so others
can build this example code without a hassle!

<ins>**1. Downloading the repository**</ins>

Start by cloning the repository with `git clone --depth 1 https://github.com/vladeemerr/vulkan-starter-app`

This repository does not contain any submodules, it utilizes CMake's `FetchContent` feature instead.

<ins>**2. Configuring the project**</ins>

Run either one of the CMake lines to download dependencies and configure the project:

```bash
cmake --preset debug      # for GNU/Linux (GCC/Clang)
cmake --preset msvc-debug # for Windows (Visual Studio 2019)
```

If you wish to build in `release` mode, change `debug` to `release`.

If changes are made (added/removed files), or if you want to regenerate project files, rerun the command above.

<ins>**3. Building**</ins>

To build the project, use the line below. You are most likely using `debug` preset, so
the directory that will eventually contain your build files is named `build-debug`.

Likewise for `release` that directory will be named `build-release`

Run one those commands, depending on which preset you chose:

```bash
cmake --build build-debug --parallel # for debug
cmake --build build-release --parallel # for release
```

### Running

`build-*` directory will contain the executable in one of the subdirectories after successful build.

For `msvc-{debug|release}` builds output subdirectory is set to `Debug` or `Release` respectively.
For other configurations output subdirectory is set to `vulkan-starter-app`.

Shader paths are configured by CMake, so the application can run from any working
directory. After moving the project, rerun CMake to update those paths.

### Laboratory controls

Select `Cube 1` or `Cube 2` to edit its position, rotation, scale and color.
Vertex colors are generated from local coordinates and multiplied by the selected color.
The projection buttons affect the whole scene.

Enable `Lemniscate animation` for the selected cube, then press `Play`.
`Pause` freezes its position and rotation on the trajectory; `Play` resumes from
that point. Speed and radius are independent for each cube. Position offsets the
trajectory, while rotation and scale remain editable during animation.

### Compiling shaders

`CMakeLists.txt` has a build recipe for compiling shader files
along with an application. Look for a comment in this file to see
how to compile your shaders.

### Laboratory 3: textures

The scene retains the camera, animation and lighting from Labs 1–2. The two
cubes use brick and luminous circuit materials; the floor uses stone tiles.
Original PNG assets and the stb_image decoder are included in the repository.

In **Objects**, select an object and choose its **Texture**. **Solid color**
preserves the earlier color controls. **UV repeat** changes texture repetition.
**Animated sampling**, **Sampling speed** and **Sampling distortion** control
the time-dependent effect. **Emission strength** controls the emissive map;
set all lights and ambient intensity to zero to see emission in darkness.

Each material has its own descriptor set. Every model uses the same dynamic
uniform buffer with a distinct aligned offset. Geometry and texture images
are copied through staging buffers into device-local GPU allocations.

Architecture and implementation plan: [docs/lab3-architecture.md](docs/lab3-architecture.md).
Asset descriptions and extension instructions: [assets/textures/README.md](assets/textures/README.md).

Optional headless checks render real frames with Vulkan validation when the
layer is installed. They require Vulkan SDK, GLM and VMA headers. The existing
application build supplies VMA at the default location; otherwise pass
`-DVMA_INCLUDE_DIR=/path/to/VulkanMemoryAllocator/include` when configuring:

```sh
cmake -S tests -B build-lab3-checks
cmake --build build-lab3-checks --config Debug
ctest --test-dir build-lab3-checks -C Debug --output-on-failure
```

These checks cover image decoding, cube UVs, texture selection, animation,
specular/emissive maps, black fallbacks and dynamic uniform buffer growth.
They save `lab3-scene.ppm` and `lab3-emissive.ppm` in the test build directory.
