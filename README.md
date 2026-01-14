# osm_opengl_rendering_example

Renders OpenStreetMap (OSM) ways tagged as highways using OpenGL geometry shaders.

[![Example](/usage.png)](https://youtu.be/3e60l875eQU)

This small demo parses an OSM XML file and renders the ways that are tagged with `highway` using Compute Shaders. The
implementation is focused on demonstrating how to render large vector map datasets on the GPU efficiently using
OpenGL (the same infrastructure available on many mobile GPUs). Osmium is used to select and load ways and nodes of interest in an efficient manner. Then a compute shader is used to extrude the ways to triangulated meshes. Finally, a simple shader program is used to render the meshes. 

# Optimizations of Note
* The Compute Shader runs in a fully parallel manner to ensure maximum performance.
* Way data is first loaded to identify which nodes are relevent and then only those nodes are loaded. This eliminates unnesessary data from being loaded and later filtered out.

**Quick summary:**
- **Input:** an OSM XML file exported from OpenStreetMap
- **Output:** an OpenGL window rendering the highway ways from the OSM file
- **Tested on:** Ubuntu (Linux). Should work on Windows and macOS with the appropriate development packages.

**Table of contents**
- [Requirements](#requirements)
- [Dependencies](#dependencies)
- [Build](#build)
- [Run](#run)
- [Notes](#notes)

## Requirements

- `cmake` (3.x)
- A C++ compiler (`clang`, `gcc`, or MSVC)
- OpenGL development headers

On Debian/Ubuntu you can install the system packages used for development with:

```bash
sudo apt update
sudo apt install build-essential cmake libgtk-3-dev libglu1-mesa-dev
```

On Fedora:

```bash
sudo dnf install cmake gcc-c++ gtk3-devel mesa-libGLU-devel
```

macOS users: install dependencies with Homebrew (for example `brew install cmake gtk+3`), then use the macOS toolchain.

## Dependencies

This project uses a number of C++ libraries (see `CMakeLists.txt` and the `build/` subproject folders). If you run into
missing dependency errors, follow the libosmium dependency guide:

https://osmcode.org/libosmium/manual.html#dependencies

The repo already includes CPM-based dependency provisioning in the CMake configuration used during configure/build.

## Export Examples
# SF Marina
Goto https://www.openstreetmap.org/export#map=17/37.804079/-122.428572 and click "Export".
Save the file to maps/sf_marina.osm 

The <COORDINATES> are `-122.436994,37.800214,-122.420150,37.807945` .

# Sausalito
Goto https://www.openstreetmap.org/export#map=16/37.85146/-122.48406 and click "Export"
Save the file to maps/sausalito.osm 

The <COORDINATES> are `-122.50035,37.84373,-122.46780,37.85918` .

## Build

Create an out-of-source build directory and build the `main` target:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j8 --target main
```

This produces the executable `build/main`.

## Run

Run the program with a path to an OSM XML file. Example:

```bash
./build/main maps/sf_marina.osm --coords=-122.436994,37.800214,-122.420150,37.807945

./build/main maps/sausalito.osm --coords=-122.50035,37.84373,-122.46780,37.85918

```

You should see an OpenGL window rendering the map ways similar to the screenshot above.

## Notes

- The demo currently renders OSM ways tagged with `highway` (roads). It is intended as an educational example of
	geometry shader usage with large vector data.
- The code has been built and run on Ubuntu; platform differences (X11/Wayland/Windows/macOS) may require different
	development packages or small build tweaks.

## Future improvements:
* preprocess the ways to lines following https://www.youtube.com/watch?v=Z-YLPoKm0Mk and extrude the intersections
* Dynamically fetch map data following [Overpass](https://tchayen.github.io/posts/fetching-data-from-the-open-street-maps) and display that instead of manual export
* Render the areas to see their shapes beneath the ways.

## License

See the `LICENSE` file in the repository for license details.

## Contact

If you have suggestions, issues, or questions, open an issue in the repository.

