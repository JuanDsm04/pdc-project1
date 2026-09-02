#pragma once

#include <omp.h>

// Shared by every OpenMP pragma in the hot path: `#pragma omp parallel for
// if(g_parallel) num_threads(g_threads)`. One code path serves both modes, so a loop runs
// on the encountering thread when g_parallel is false and spreads across g_threads
// otherwise. App initializes both from the validated --serial/--parallel and --threads
// options, and the P key changes g_parallel only between complete frames.
extern bool g_parallel;
extern int  g_threads;
