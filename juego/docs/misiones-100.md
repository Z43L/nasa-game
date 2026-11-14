# Especificación de las misiones 7 a 106 (100 misiones nuevas)

## Formato
Cada misión es una llamada en `src/mis_gN.lg` (N = 1..5) dentro de `fn mis_gN() -> List[MisionDato]`:

    l.append(mis_dato(NUMERO, LIMITE_S, RACHA, "dif.facil", "tipo.comando",
        [mis_av("reactor", "av.inyector", 0, 0)],
        [mis_cond("reactor", 1, 0, 0), mis_cond("energia", 0, 0, 30)]))

Ver `src/mision_modelo.lg`. Campos:
- `mis_av(sistema, clave, deriva, valor)`: avería inicial. `clave` es `av.*` (usa una existente o crea `av.gN.<nombre>`). `deriva` cambia el valor del sistema cada segundo mientras dure la avería. `valor` 0 = no cambia el valor inicial.
- `mis_cond(sistema, tipo, op, valor)`: condición del objetivo (todas deben cumplirse a la vez). tipo 0 = valor numérico con op 0 (>=), 1 (<=) o 2 (==); tipo 1 = sistema sano (activo, sin avería, en rango); tipo 2 = activo.
- `racha`: segundos seguidos que debe cumplirse el objetivo (1 = en un instante; 10 o 15 para misiones de mantenimiento).
- `dificultad`: `dif.facil`, `dif.media`, `dif.dificil` o `dif.extrema`.
- `tipo`: `tipo.comando` (se resuelve solo con órdenes de terminal), `tipo.programar` (hay que escribir un script con `import nave`), `tipo.ambas` (órdenes y script).

## Sistemas y reglas reales (leer src/sim.lg y src/misiones.lg antes de diseñar)
- Sistemas: reactor (temperatura, rango 300-650, falla si supera 650), refrigeracion (potencia 0-100, regulada a su objetivo), energia (0-100, falla si llega a 0), oxigeno (kPa, rango 95-110, regulado a 101), comunicaciones (señal 60-100, regulada), co2 (rango 0-8, falla si supera 8, sin avería vuelve a 3 a 1/s), hidrogeno (20-100, estática).
- Cada segundo: la refrigeración sana enfría el reactor (valor/8 por segundo) y el reactor se calienta 3/s si está activo. Cada sistema activo consume 1 de energía (refrigeración, oxígeno y comunicaciones); el reactor sano recarga 3/s. Sin energía (<10) las comunicaciones pierden 5/s.
- Una avería con deriva suma `deriva` al valor cada segundo.
- Reparar exige diagnosticar antes (`diagnostico SISTEMA`, después `reparar SISTEMA`). Encender/apagar: `encender SISTEMA`, `apagar SISTEMA`. Ajustar objetivo: `ajustar SISTEMA VALOR` (solo refrigeracion, oxigeno y comunicaciones). Esperar: `esperar N`.
- Scripts: `import nave`; `nave_estado`, `nave_valor`, `nave_diagnostico`, `nave_reparar`, `nave_encender`, `nave_apagar`, `nave_ajustar`, `nave_esperar`, `nave_mision` (ver lib/nave.lg). Un script corre como proceso con 60 s de límite y se ejecuta con `ejecutar NOMBRE.lg` desde la terminal. Está en scripts/ y se llama así: `scripts/mision_N.lg`.
- Fallos de la misión: reactor > 650, energía 0, co2 > 8, o tiempo agotado.

## Reglas de diseño
1. **Resoluble**: toda misión tiene una solución que cumple el objetivo antes del límite. Una solución de tipo comando debe funcionar con el simulador (ver abajo).
2. **Sin trampas**: el objetivo no puede cumplirse desde el principio. Debe requerir actuar.
3. **Dificultad real**: facil = una o dos averías, límite holgado (180-240 s), racha 1. media = dos averías o una con deriva rápida, límite 150-200 s. dificil = averías con deriva y/o racha de 5-10 s, límite 120-160 s. extrema = tres averías o más, o racha de 10-15 s, límite 90-140 s.
4. **Tipos**: al menos la mitad de las misiones de tipo comando; el resto, programar o ambas. Un script debe tener un bucle que vigila el sistema (nave_estado / nave_valor) y actúa (nave_diagnostico / nave_reparar / nave_ajustar).
5. **Textos**: título corto y briefing de 1 a 3 frases, con el objetivo dicho explícitamente y los nombres internos de sistema entre comillas simples (como las misiones 1 a 6). Escribe los cuatro idiomas (es, en, fr, ja) de forma natural, no traducción literal.

## Verificación obligatoria (comandos)
Simulador determinista: `tests/sim_mision.lg`. Compila desde juego/:

    ../lenguaje/build/bin/lgc tests/sim_mision.lg -I src -I lib -I ../lenguaje/lib/nucleo -I ../lenguaje/lib/datos -I ../lenguaje/stdlib -o <tu_binario>

Entrada por stdin: primera línea el número de misión; luego una línea `segundo|orden` por cada orden (p. ej. `0|diagnostico refrigeracion`, `1|reparar refrigeracion`); línea vacía para terminar. Salida: `RESULTADO OK t=...` o `RESULTADO ERR ...`.
Guarda la entrada de cada misión de tipo comando o ambas en `tests/soluciones/mision_N.txt` (mismo formato). Para misiones de tipo programar la solución de comandos no es necesaria; el script se valida en directo más tarde.

## Ficheros propios (solo tuyos)
- `src/mis_gN.lg` (tu grupo).
- `i18n/pendientes/mis_gN.json`: claves `mision.N.titulo` y `mision.N.briefing` para tus misiones, más cualquier `av.gN.*` que crees, en es, en, fr y ja.
- `tests/soluciones/mision_N.txt` para tus misiones de tipo comando o ambas.
- `scripts/mision_N.lg` para tus misiones de tipo programar o ambas.

No edites: misiones.lg, sim.lg, mision_modelo.lg, shell.lg, traducciones.json, main.lg, ni los ficheros de otros grupos.
