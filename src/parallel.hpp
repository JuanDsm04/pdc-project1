#pragma once

// Shared by every OpenMP pragma in the hot path: `#pragma omp parallel for
// if(g_parallel) num_threads(g_threads)`. One code path serves both modes, so a loop runs
// on the encountering thread when g_parallel is false and spreads across g_threads
// otherwise. The CLI at step 15 will set these from --serial and --threads; until then
// they default to parallel across every hardware thread the machine reports.
extern bool g_parallel;
extern int  g_threads;