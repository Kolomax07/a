# Controlador de cultivo — versión ESP32

Firmware para ESP32 con los mismos cuatro sistemas que la versión de Raspberry Pi: **luz** (fotoperiodo), **humedad** (humidificador, deshumidificador y extractor), **riego** por humedad del sustrato y **nutrientes** (EC con partes A+B y corrección de pH). Además, un **panel web** para verlo y manejarlo desde el móvil.

| | |
|---|---|
| **Panel web** | `http://cultivo.local`: lecturas en vivo, estado de cada equipo, alarmas, últimos eventos y cambio de etapa. Incluye la página `/calibrar` y la API JSON `/api/estado`. |
| **Etapa** | Se cambia desde el panel y se guarda en la memoria del ESP32, así que sobrevive a cortes de luz. |
| **Hora** | Por NTP vía WiFi, con cambio de horario verano/invierno. Opcionalmente un RTC DS3231 con pila, para mantener el fotoperiodo sin internet. |
| **Seguridad** | Todos los relés apagados al arrancar. Cada sistema pasa a estado seguro si falla un sensor. Hay límites diarios de riegos y dosis, y un watchdog reinicia el ESP32 si el programa se cuelga. |
| **Sin bloqueos** | Las bombas funcionan por pulsos programados, así que el panel y el control siguen respondiendo mientras se riega o se dosifica. |

La lógica de control vive en `lib/cultivo/`, sin dependencias de Arduino, y es la misma que la de la versión Python, con sus mismos tests.

> **Estado:** la lógica de control está probada con 42 tests en el PC, incluida una simulación de varios días con todas las etapas. El firmware completo **todavía no se ha probado en una placa real**: pruébalo primero sin plantas.

## Material

| Componente | Modelo sugerido | Notas |
|---|---|---|
| Placa | ESP32 DevKit (ESP32-WROOM-32) | |
| Temperatura y HR | SHT31 (I2C) | o DHT22 |
| Luz (opcional) | BH1750 (I2C) | Detecta lámparas fundidas y fugas de luz |
| Conversor analógico | ADS1115 (I2C) | Recomendado para pH/EC. Se puede usar el ADC interno, pero es menos preciso. |
| Reloj (opcional) | DS3231 (I2C) | Mantiene la hora sin WiFi |
| Humedad del sustrato | Sensor capacitivo v1.2 | No uses los resistivos: se corroen |
| EC | DFRobot Gravity EC (DFR0300) | |
| pH | DFRobot Gravity pH v2 | Usa un aislador galvánico (DFR0504) si EC y pH van en el mismo depósito |
| Nivel del depósito | Interruptor de flotador | |
| Relés | Módulo de 8 relés + 1, o relés SSR | Ver la nota de 3,3 V |
| Bombas | Sumergible de riego + 4 peristálticas de 12 V | |

## Conexiones

Los sensores se alimentan a **3,3 V**.

| Componente | ESP32 |
|---|---|
| SHT31, BH1750, ADS1115, DS3231 | I2C: SDA → GPIO21, SCL → GPIO22, VCC → 3V3, GND |
| Sensor de sustrato / sonda EC / sonda pH | ADS1115 A0 / A1 / A2. Con el ADC interno, GPIO 34 / 35 / 36. |
| DHT22 (alternativa al SHT31) | GPIO13 |
| Flotador | Entre GPIO4 y GND |
| Relés | Luz GPIO16 · Riego GPIO17 · Humidificador GPIO18 · Deshumidificador GPIO19 · Extractor GPIO23 · Nutriente A GPIO25 · Nutriente B GPIO26 · pH+ GPIO27 · pH- GPIO33 |

Todo se cambia en `src/config.h`. Pon `-1` en lo que no tengas instalado.

> **Relés y 3,3 V:** muchos módulos de relés de 5 V con optoacoplador no llegan a apagarse con los 3,3 V del ESP32. Usa módulos compatibles con 3,3 V, o quita el puente JD-VCC y conecta VCC a 3,3 V y JD-VCC a 5 V.
>
> ⚠️ **Seguridad eléctrica:** el agua y los 230 V son una combinación peligrosa. Instala un diferencial, mete los relés en una caja cerrada lejos del agua y comprueba que aguantan la potencia de cada equipo. Si no tienes experiencia con instalaciones de red, usa enchufes con relé homologados.

## Puesta en marcha

Necesitas [PlatformIO](https://platformio.org/install), como extensión de VS Code o desde la línea de comandos.

```bash
cd esp32
cp src/secretos.ejemplo.h src/secretos.h   # pon tu WiFi y, si quieres, una contraseña para el panel
# edita src/config.h: zona horaria, medio de cultivo, hora de encendido, pines...
pio run -t upload                            # compila y sube al ESP32
pio device monitor                           # muestra eventos y lecturas por el puerto serie
```

Al conectar a la WiFi, el monitor serie muestra la dirección del panel. Desde el móvil, en la misma red, abre `http://cultivo.local` (o la IP que aparezca).

### Antes de conectar las plantas

1. **Relés:** con los equipos desenchufados, pulsa **Probar** en cada equipo del panel. El relé debe hacer clic durante 2 s.
2. **Sondas:** abre `http://cultivo.local/calibrar`, que muestra los voltajes en vivo.
   - Sustrato: anota el voltaje con el sensor al aire (`sueloVSeco`) y sumergido en agua (`sueloVMojado`).
   - pH: tampón pH 7 (`phV7`) y tampón pH 4 (`phV4`).
   - EC: agua destilada (`ecV0`) y patrón de 1,413 mS/cm (`ecV1413`).

   Pon los valores en `calibracionSondas()` de `src/config.h` y vuelve a subir el firmware.
3. **Peristálticas:** mide cuántos ml echan en 10 s y pon el caudal en `caudalMlS`.
4. **Depósito:** prepáralo a mano cerca de la EC objetivo. El controlador mantiene la EC con dosis pequeñas; no está pensado para mezclar un depósito entero desde cero.

## Objetivos por etapa (por defecto)

| Etapa | Luz | HR | Extractor desde | EC (mS/cm) |
|---|---|---|---|---|
| Plántula | 18/6 | 65–75 % | 27 °C | 0,6 |
| Vegetativo | 18/6 | 50–65 % | 28 °C | 1,2 |
| Floración | 12/12 | 40–50 % | 26 °C | 1,6 |
| Floración tardía | 12/12 | 35–45 % | 25 °C | 1,4 |
| Lavado | 12/12 | 35–45 % | 25 °C | — (solo pH) |

Se ajustan en la tabla `OBJETIVOS` de `src/config.h`. Rango de pH según el medio: tierra 6,2–6,8 · coco 5,7–6,3 · hidro 5,6–6,2.

## Si falla algo

| Situación | Qué hace |
|---|---|
| Sin WiFi | Sigue controlando. Con el DS3231 mantiene el horario. |
| Sin hora (sin WiFi ni RTC) | Luz apagada (en floración es menos dañino que encenderla de noche), riega sin mirar el horario y muestra una alarma |
| Falla el sensor de clima | Humidificador y deshumidificador apagados, extractor encendido |
| Falla el sensor de sustrato | No riega y muestra una alarma |
| Lectura de EC o pH imposible, o sonda fuera del agua | No dosifica y muestra una alarma |
| Depósito bajo | No riega ni dosifica |
| El programa se cuelga | El watchdog reinicia el ESP32, que arranca con los relés apagados |

## Integración con Home Assistant

`/api/estado` devuelve todas las lecturas, relés, alarmas y contadores en JSON:

```yaml
# configuration.yaml
rest:
  - resource: http://cultivo.local/api/estado
    scan_interval: 60
    sensor:
      - name: Cultivo temperatura
        value_template: "{{ value_json.lecturas.temperatura }}"
        unit_of_measurement: "°C"
      - name: Cultivo EC
        value_template: "{{ value_json.lecturas.ec }}"
```

## Tests en el PC

```bash
pio test -e native
```

Ejecuta los tests de cada controlador y una simulación de varios días del armario completo en todas las etapas. No hace falta la placa.

## Estructura

```
esp32/
├── platformio.ini
├── src/
│   ├── config.h           ← toda la configuración
│   ├── secretos.ejemplo.h  WiFi y contraseña del panel (cópialo como secretos.h)
│   ├── main.cpp           arranque, bucle y WiFi
│   ├── hardware.cpp       sensores y relés
│   ├── reloj.cpp          NTP, zona horaria y RTC
│   └── panel_web.cpp      panel, calibración y API JSON
├── lib/cultivo/src/       lógica de control (C++ puro, sin Arduino)
└── test/                  tests con Unity
```
