#include "parallel.hpp"

#include <omp.h>

bool g_parallel = true;
int  g_threads  = omp_get_max_threads();