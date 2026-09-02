# Infinity Gauntlet Screensaver

Salvapantallas 3D en C++17 que simula las seis Gemas del Infinito. Las partículas se
actualizan y renderizan en CPU; OpenMP paraleliza la física, proyección, particionado y
rasterización, mientras SDL2 presenta el framebuffer y SDL2_ttf dibuja el HUD.

El mismo ejecutable permite comparar la versión secuencial y la paralela sin mantener dos
bases de código distintas.

## Dependencias

Ubuntu o Debian:

```bash
sudo apt update
sudo apt install build-essential pkg-config libsdl2-dev libsdl2-ttf-dev
```

Arch Linux:

```bash
sudo pacman -S base-devel pkgconf sdl2 sdl2_ttf
```

## Compilar y ejecutar

```bash
make -j$(nproc)
./bin/gauntlet 100000
```

`100000` es el parámetro `N`: la cantidad de partículas renderizadas. También puede
escribirse explícitamente:

```bash
./bin/gauntlet --particles 100000
```

Ejemplos de comparación:

```bash
./bin/gauntlet --particles 200000 --serial --no-vsync
./bin/gauntlet --particles 200000 --parallel --threads 8 --no-vsync
```

## Parámetros

| Opción | Descripción |
|---|---|
| `N`, `-n N`, `--particles N` | Partículas, de 1 a 1,000,000; valor inicial 100,000. |
| `--width W` | Ancho de 640 a 3,840; valor inicial 1,280. |
| `--height H` | Alto de 480 a 2,160; valor inicial 720. |
| `-t T`, `--threads T` | Hilos OpenMP, entre 1 y los procesadores disponibles. |
| `--serial` | Inicia ejecutando los kernels de forma secuencial. |
| `--parallel` | Inicia con OpenMP; es el modo predeterminado. |
| `--seed S` | Semilla determinista de 32 bits. |
| `--no-vsync` | Desactiva el límite del refresco del monitor. |
| `--benchmark` | Ejecuta las mediciones CPU sin crear una ventana. |
| `--bench-frames F` | Frames medidos en cada prueba; valor inicial 120. |
| `--bench-runs R` | Repeticiones por cantidad de hilos, mínimo 10. |
| `-h`, `--help` | Muestra la ayuda completa. |

Los argumentos desconocidos, incompletos o fuera de rango terminan con código de error y
un mensaje explicativo antes de reservar memoria o inicializar la ventana.

## Controles

| Tecla | Acción |
|---|---|
| `P` | Alternar en vivo entre ejecución secuencial y paralela. |
| `SPACE` | Activar manualmente el Snap. |
| `ESC` | Cerrar limpiamente el programa. |

El HUD muestra FPS, modo activo, hilos utilizados y número de partículas.

## Benchmark, speedup y eficiencia

El benchmark repite cada prueba diez veces como mínimo. La prueba de un hilo utiliza el
camino secuencial; las demás utilizan OpenMP. Todas parten de la misma semilla y ejecutan
el mismo número de frames después de un calentamiento no medido.

```bash
mkdir -p results
./bin/gauntlet --benchmark --particles 200000 --threads 8 \
    --bench-frames 300 --bench-runs 10 > results/scaling.csv
```

También existe un objetivo preparado:

```bash
make benchmark
```

El CSV contiene cada medición y los valores calculados con:

```text
speedup(p)    = tiempo_promedio(1) / tiempo_promedio(p)
eficiencia(p) = speedup(p) / p
```

La salida incluye las columnas `threads`, `measurement`, `seconds`, `fps`,
`mean_seconds`, `speedup` y `efficiency`. Los mensajes de progreso se escriben en stderr,
por lo que no contaminan el CSV redirigido.

## Paralelización y sincronización

- El estado de partículas usa estructura de arreglos para mantener accesos contiguos.
- Física, integración, proyección y construcción de splats se dividen por partículas.
- El grid y las cámaras usan histogramas privados por hilo, suma prefija exclusiva y
  scatter paralelo determinista.
- Los conteos de Soul, Space y Snap utilizan reducciones OpenMP.
- Cada hilo escribe bins privados durante el binning.
- La rasterización asigna tiles completos a un solo hilo; dos hilos nunca escriben el
  mismo píxel, por lo que no necesita atomics ni critical sections.
- Las barreras implícitas al terminar cada `omp parallel for` separan las etapas.
- `if(g_parallel)` permite ejecutar exactamente los mismos kernels en modo secuencial.

## Limpiar

```bash
make clean
```

Para la entrega se incluyen el código fuente, este README, el Makefile y los assets. No se
entregan `build/`, `bin/`, ejecutables ni archivos CSV generados.
