# Infinity Gauntlet Screensaver

A 3D screensaver written in C++ with OpenMP and SDL2 for the parallel computing course.

Six Infinity Stones move through a bounded volume, and a configurable number of particles
react to each stone's power. Physics, 3D projection and rasterization all run on the CPU
so that OpenMP thread count maps directly to frame rate.

## Build

```
make
./bin/gauntlet
```

Requires SDL2, OpenMP and a C++17 compiler. On Arch:

```
sudo pacman -S sdl2
```

## Status

Work in progress.
