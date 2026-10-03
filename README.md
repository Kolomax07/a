# Controlador de cultivo de cannabis

Controlador automático para un armario de cultivo, con cuatro sistemas:

| Sistema | Qué hace | Sensores → Actuadores |
|---|---|---|
| **Luz** | Fotoperiodo según la etapa (18/6 en vegetativo, 12/12 en floración), aunque cruce la medianoche. Avisa si la lámpara no enciende o si entra luz durante la noche, porque en floración eso puede volver hermafroditas a las plantas. | BH1750 (opcional) → relé de la lámpara |
| **Humedad** | Mantiene la HR en el rango de la etapa con histéresis. Nunca enciende humidificador y deshumidificador a la vez y respeta un tiempo mínimo entre arranques para proteger compresores y relés. El extractor actúa por exceso de calor o de humedad. | SHT31 o DHT22 → humidificador, deshumidificador, extractor |
| **Riego** | Riega cuando el sustrato baja del umbral y espera a que el agua se reparta antes de volver a regar. Tiene un máximo de riegos al día, no riega de noche (configurable) y no arranca la bomba si el depósito está vacío. | Sensor capacitivo + flotador → bomba de riego |
| **Nutrientes** | Mantiene la EC y el pH del depósito con dosis pequeñas y esperas de mezcla. Primero ajusta la EC (parte A, pausa, parte B) y después el pH. Tiene límites diarios de dosis y descarta las lecturas imposibles para que una sonda rota no vacíe los bidones. | Sondas EC y pH → 4 bombas peristálticas (A, B, pH+, pH-) |

Si falla un sensor, cada sistema pasa a un **estado seguro**, y al parar el programa se **apagan todos los relés**.

> ⚖️ Comprueba la legislación de tu país sobre el autocultivo antes de usarlo.

## Dos versiones

Las dos usan la misma lógica de control y los mismos tests:

| Versión | Dónde | Ventajas |
|---|---|---|
| **ESP32** | [`esp32/`](esp32/README.md) | Placa barata, panel web desde el móvil, arranque instantáneo y nada que mantener |
| **Raspberry Pi** (Python) | este directorio | Histórico CSV completo y simulador para probar configuraciones en el PC |

El resto de este documento describe la versión Python. La del ESP32 tiene su propio [README](esp32/README.md).

La versión Python guarda además un **histórico CSV** con todas las lecturas (incluido el VPD), los estados de los relés, las acciones y las alarmas.

## Probarlo sin hardware (simulación)

Solo necesitas Python 3.11 o superior y ninguna dependencia:

```bash
python3 -m cultivo --simular --dias 3
```

```
2026-10-03 00:00:00 INFO    Dosis dosificador_a 2 s (EC 0.89, pH 6.40)
2026-10-03 00:00:32 INFO    Dosis dosificador_b 2 s (EC 0.89, pH 6.40)
...
2026-10-03 06:00:00 INFO    Luz encendida
2026-10-03 09:03:00 INFO    Riego 20 s (sustrato al 35 %)
...
Simulación de 3 días — etapa vegetativo, medio coco
  Humedad relativa: 99 % del tiempo en 50–65 % (mín 49.2, máx 56.7)
  Temperatura: 20.7–28.2 °C (extractor a partir de 28 °C)
  Sustrato: mínimo 33.0 % (riega por debajo de 35 %), 7 riegos
  EC final 1.15 mS/cm (objetivo 1.2), 12 dosis de nutrientes
  pH final 6.11 (rango 5.7–6.3), 10 correcciones
  Alarmas: ninguna
  Histórico completo en simulacion.csv
```

El simulador es un armario virtual: la lámpara calienta, la planta transpira y consume agua y nutrientes, y el pH sube con el tiempo. Sirve para probar tu configuración antes de conectar nada:

```bash
cp config.ejemplo.toml config.toml        # edita etapa, horarios, umbrales...
python3 -m cultivo --simular -c config.toml --dias 7 -v
```

Tests: `python3 -m unittest discover -s tests -t .`

## Material

| Componente | Modelo sugerido | Notas |
|---|---|---|
| Controlador | Raspberry Pi 3/4/5 o Zero 2 W | Raspberry Pi OS |
| Temperatura y HR | SHT31 (I2C) | Más fiable que el DHT22, aunque este también es compatible |
| Luz (opcional) | BH1750 (I2C) | Detecta lámparas fundidas y fugas de luz |
| Conversor analógico | ADS1115 (I2C) | La Raspberry no tiene entradas analógicas |
| Humedad del sustrato | Sensor capacitivo v1.2 | No uses los resistivos: se corroen en semanas |
| EC | DFRobot Gravity EC (DFR0300) | |
| pH | DFRobot Gravity pH v2 (SEN0161-V2) | Usa un aislador galvánico (DFR0504) si EC y pH van en el mismo depósito |
| Nivel del depósito | Interruptor de flotador | Protege la bomba y las sondas |
| Relés | Módulo de 8 relés + 1 extra, o relés SSR | Para 230 V, mejor relés con caja o enchufes con relé |
| Bomba de riego | Bomba sumergible + goteros | |
| Dosificación | 4 bombas peristálticas de 12 V | Calibra el caudal (ml/s) |

## Conexiones

Todos los sensores se alimentan a **3,3 V**. Así las señales nunca superan lo que aguantan el ADS1115 y los GPIO.

| Componente | Conexión |
|---|---|
| SHT31, BH1750, ADS1115 | Bus I2C: SDA → GPIO2, SCL → GPIO3, VCC → 3,3 V, GND |
| ADS1115 ADDR | A GND (dirección 0x48) |
| Sensor de sustrato | ADS1115 A0 |
| Sonda EC | ADS1115 A1 |
| Sonda pH | ADS1115 A2 |
| Flotador | Entre GPIO17 y GND (pull-up interno) |
| DHT22 (alternativa al SHT31) | Datos → GPIO4 |
| Relés (IN1…IN9) | Luz GPIO5 · Riego GPIO6 · Humidificador GPIO13 · Deshumidificador GPIO19 · Extractor GPIO26 · Nutriente A GPIO12 · Nutriente B GPIO16 · pH+ GPIO20 · pH- GPIO21 |

Todos los pines se cambian en `[hardware]` dentro de `config.toml`. Si no tienes algún equipo, borra su línea en `[hardware.reles]` o pon el canal a `-1`.

> ⚠️ **Seguridad eléctrica**: el agua y los 230 V son una combinación peligrosa. Instala un diferencial, mete los relés de red en una caja cerrada y lejos del agua, y comprueba que los relés aguantan la potencia de cada equipo (las lámparas LED y los deshumidificadores tienen picos de arranque). Si no tienes experiencia con instalaciones de red, usa enchufes con relé ya homologados.

## Instalación en la Raspberry Pi

```bash
sudo raspi-config nonint do_i2c 0                  # activa el bus I2C
git clone <este repositorio> ~/cultivo && cd ~/cultivo
python3 -m venv --system-site-packages .venv       # gpiozero y lgpio vienen con el sistema
.venv/bin/pip install -r requirements-raspberry.txt
cp config.ejemplo.toml config.toml
```

1. **Prueba los relés**, todavía sin equipos enchufados: `.venv/bin/python -m cultivo -c config.toml --probar-reles`
2. **Calibra las sondas**: `.venv/bin/python -m cultivo -c config.toml --calibrar` muestra los voltajes en vivo.
   - Sustrato: anota el voltaje con el sensor al aire (`suelo_v_seco`) y sumergido en agua (`suelo_v_mojado`).
   - pH: sonda en tampón pH 7 (`ph_v_7`), aclarar, y en tampón pH 4 (`ph_v_4`).
   - EC: sonda en agua destilada (`ec_v_0`) y en patrón de 1,413 mS/cm (`ec_v_1413`).
   - Bombas peristálticas: mide cuántos ml echan en 10 s y pon `caudal_ml_s`.
3. **Simula** tu `config.toml` (`--simular`) para comprobar que los umbrales tienen sentido.
4. **Arranca** el controlador: `.venv/bin/python -m cultivo -c config.toml`

### Como servicio (arranque automático)

```bash
sudo cp cultivo.service /etc/systemd/system/      # revisa usuario y rutas dentro
sudo systemctl enable --now cultivo
journalctl -u cultivo -f                          # ver el log en vivo
```

Para cambiar de etapa, edita `etapa` en `config.toml` y ejecuta `sudo systemctl restart cultivo`.

El fotoperiodo depende del reloj del sistema. Sin internet (y por tanto sin NTP), añade un reloj RTC DS3231.

## Objetivos por etapa (valores por defecto)

| Etapa | Luz | HR | Extractor desde | EC (mS/cm) |
|---|---|---|---|---|
| `plantula` | 18/6 | 65–75 % | 27 °C | 0,6 |
| `vegetativo` | 18/6 | 50–65 % | 28 °C | 1,2 |
| `floracion` | 12/12 | 40–50 % | 26 °C | 1,6 |
| `floracion_tardia` | 12/12 | 35–45 % | 25 °C | 1,4 |
| `lavado` | 12/12 | 35–45 % | 25 °C | — (solo corrige pH) |

pH según `medio`: `tierra` 6,2–6,8 · `coco` 5,7–6,3 · `hidro` 5,6–6,2.

Son valores de partida habituales. Ajústalos a tu genética y a la tabla de tu marca de fertilizantes en `[etapas.<nombre>]`.

## Alarmas

Cada alarma se escribe en el log una vez al aparecer (`ALARMA: ...`) y otra al resolverse. También queda en la columna `alarmas` del CSV:

- Sensor sin lectura (cada sistema pasa a su estado seguro)
- Lámpara que no enciende / luz durante la noche
- Temperatura crítica
- Depósito bajo (riego y dosificación bloqueados)
- Límite diario de riegos o de dosis alcanzado (posible avería de bomba o sonda)
- EC alta (hay que añadir agua) y lecturas de EC o pH imposibles

## Estructura

```
cultivo/
├── __main__.py           línea de comandos
├── config.py             carga y validación del TOML
├── etapas.py             objetivos por etapa y rangos de pH
├── controlador.py        bucle principal que coordina los cuatro sistemas
├── controladores/        lógica pura de luz, humedad, riego y nutrientes
├── hardware/
│   ├── base.py           interfaz común (lectura, relés, pulsos seguros)
│   ├── raspberry.py      drivers reales (gpiozero + Adafruit)
│   ├── calibracion.py    voltaje → % sustrato, pH, EC compensada
│   └── simulado.py       armario virtual
├── registro.py           CSV y gestión de alarmas
└── simulacion.py         ejecución simulada y resumen
tests/                    51 tests (unittest)
```

Los controladores no tocan el hardware: reciben `Lecturas` y devuelven decisiones. Así se prueban sin Raspberry Pi y se pueden portar a otra placa implementando una subclase de `Hardware`.
