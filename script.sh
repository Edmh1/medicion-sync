#!/usr/bin/env bash
# -----------------------------------------------------------
# script.sh  –  Script de automatización de pruebas
# -----------------------------------------------------------
# Recorre tres programas (barrera, semáforo y espera‑activa),
# los ejecuta con tres valores distintos de n_hilos (3, 30, 300)
# y repite cada combinación 5 veces.  Para cada corrida captura:
#   * Tiempo total (segundos) – impreso por el programa
#   * Latencia (segundos/línea) – impreso por el programa
#   * Throughput (líneas/segundo) – impreso por el programa
#   * Porcentaje de CPU – tomado de /usr/bin/time -v
# Finalmente calcula el promedio y la desviación estándar de
# cada métrica y genera un JSON con la estructura requerida.
# -----------------------------------------------------------
# Uso: ./script.sh <archivo_de_prueba.c>
# Ej.: ./script.sh test1mb.txt
# -----------------------------------------------------------

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Uso: $0 <archivo_de_prueba.c>" >&2
  exit 1
fi

INPUT_C=$1
if [[ ! -f "$INPUT_C" ]]; then
  echo "[Error] El archivo $INPUT_C no existe" >&2
  exit 1
fi

# ----- CONFIGURACIÓN ------------------------------------------------
PROGRAMS=("barrera" "semaforo" "espera")           # ejecutables finales
SOURCES=("analizador_barrera.c" "analizador_sem.c" "analizador_esp.c")      # archivos fuente
THREADS=(3 30 300)                                   # valores de n_hilos
RUNS=5                                               # repeticiones
CSV_TMP="__bench_tmp.csv"
JSON_OUT="resultados.json"
# -------------------------------------------------------------------

echo "# Compilando ejecutables si es necesario …"
for i in "${!PROGRAMS[@]}"; do
  exe="${PROGRAMS[$i]}"
  src="${SOURCES[$i]}"
  if [[ ! -x "$exe" || "$src" -nt "$exe" ]]; then
    echo "  • gcc $src  -O2 -pthread  -o $exe"
    gcc "$src" -O2 -pthread -o "$exe"
  fi
done

echo "program,threads,run,tiempo,latencia,throughput,cpu" > "$CSV_TMP"

# ----- BUCLE PRINCIPAL DE PRUEBAS ----------------------------------
for i in "${!PROGRAMS[@]}"; do
  exe="${PROGRAMS[$i]}"
  for th in "${THREADS[@]}"; do
    for ((r=1; r<=RUNS; r++)); do
      echo "\n▶ Ejecutando $exe  hilos=$th  correr=$r" >&2

      # Limpiar caché antes de cada ejecución
      sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"

      # Ejecutamos /usr/bin/time -v y capturamos stdout+stderr
      OUTPUT=$( /usr/bin/time -v ./$exe "$INPUT_C" "$th" 2>&1 )

      # Extraer métricas con grep + expresiones regulares
      tiempo=$(echo "$OUTPUT" | grep -m1 -oE 'Tiempo total: *[0-9.]+' | awk '{print $3}')
      latencia=$(echo "$OUTPUT" | grep -m1 -oE 'Latencia: *[0-9.]+' | awk '{print $2}')
      throughput=$(echo "$OUTPUT" | grep -m1 -oE 'Throughput: *[0-9.]+' | awk '{print $2}')
      cpu=$(echo "$OUTPUT" | grep -m1 -oE 'Percent of CPU this job got: *[0-9.]+' | awk '{print $7}')

      # Validación rápida (si grep falla, las variables estarán vacías)
      if [[ -z "$tiempo" || -z "$latencia" || -z "$throughput" || -z "$cpu" ]]; then
        echo "[ADVERTENCIA] No se pudieron parsear todas las métricas, se omite esta corrida." >&2
        continue
      fi

      # Registrar línea CSV
      echo "$exe,$th,$r,$tiempo,$latencia,$throughput,$cpu" >> "$CSV_TMP"
    done
  done
done

echo "\n# Procesando resultados (promedio y desviación estándar) …" >&2

python3 - "$CSV_TMP" "$JSON_OUT" << 'PY'
import csv, json, math, sys, collections, statistics, pathlib
csv_file, json_out = sys.argv[1:3]

data = collections.defaultdict(lambda: collections.defaultdict(list))
# Estructura: data[(program,threads)][metric] -> list of values

with open(csv_file) as f:
    reader = csv.DictReader(f)
    for row in reader:
        key = (row['program'], int(row['threads']))
        for metric in ('tiempo', 'latencia', 'throughput', 'cpu'):
            data[key][metric].append(float(row[metric]))

results = []
for (prog, threads), metrics in sorted(data.items()):
    aggregated = {}
    for metric, values in metrics.items():
        if len(values) == 0:
            continue
        mean = statistics.mean(values)
        stdev = statistics.pstdev(values)  # desviación estándar poblacional
        aggregated[metric] = {'avg': round(mean, 6), 'std': round(stdev, 6)}
    results.append({
        'program': prog,
        'threads': threads,
        'runs': len(next(iter(metrics.values()))),
        'metrics': aggregated
    })

with open(json_out, 'w') as f:
    json.dump(results, f, indent=2, sort_keys=False)

print(f"JSON generado → {json_out} \n")
print(json.dumps(results, indent=2))
PY

# Limpieza temporal
rm -f "$CSV_TMP"

echo "\n Pruebas completadas. Revisa $JSON_OUT para ver los promedios y desviaciones estándar."
