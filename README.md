# Análisis de Desempeño de Mecanismos de Sincronización en Programas Concurrentes en C

## Descripción del proyecto
Este proyecto estudia y **compara el rendimiento** de tres mecanismos de sincronización en C (barreras, espera activa y semáforos) al procesar datos con **hilos POSIX (pthreads)**.  
Se implementa **un único archivo ejecutable**: un hilo maestro distribuye el trabajo y varios hilos trabajadores lo procesan.

Las pruebas disponibles son 3:

| Archivo de prueba | Tamaño |
|-------------------|--------|
| `test10kb.txt`    | 10 KB  |
| `test600kb.txt`   | 600 KB |
| `test1mb.txt`     | 1 MB   |

Para cada archivo se experimenta con 3, 30 y 300 hilos.

## Versiones implementadas
| Versión            | API / Técnica                         | Descripción breve |
|--------------------|---------------------------------------|-------------------|
| **Barreras**       | `pthread_barrier_t`                   | Los hilos esperan en un punto de sincronización antes de continuar. |
| **Espera activa**  | Bucle `while` protegido con `pthread_mutex_t` | Los hilos giran («spin-wait») hasta que una condición se cumple, manteniendo el mutex brevemente. |
| **Semáforos**      | `sem_t`                               | Controlan la entrada y permiten que el hilo se bloquee, reduciendo el uso de CPU. |

## Métricas evaluadas
- Tiempo total de ejecución  
- Latencia por hilo  
- Throughput (líneas procesadas por segundo)  
- Uso de CPU

Cada combinación (archivo × hilos) se ejecuta **cinco veces** para obtener media y desviación estándar.

## Metodología de pruebas
1. Implementar las tres versiones en un solo ejecutable, seleccionable con banderas de línea de comandos.  
2. Registrar métricas y volcar los resultados a archivos JSON.  

---

## Instrucciones de uso

```bash
# 1. Verificar que Python 3 esté disponible
python3 --version

# 2. Dar permisos de ejecución al script de pruebas
chmod +x script.sh

# 3. Ejecutar con privilegios para perf (pedirá contraseña)
sudo ./script.sh test10kb.txt

# 4. Ingresar la contraseña de sudo cuando se solicite

# 5. Revisar el archivo de resultados generado
cat resultados_test10kb.json

#OPCIONAL Instalar (si no está presente) y abrir el monitor de GNOME
sudo apt install gnome-system-monitor   # Para distribuciones Debian/Ubuntu
gnome-system-monitor
