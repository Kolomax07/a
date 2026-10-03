"""Carga y validación de la configuración (archivo TOML)."""

from __future__ import annotations

import tomllib
from dataclasses import dataclass, field, fields, replace
from pathlib import Path
from typing import Any

from .etapas import ETAPAS, RANGOS_PH, ObjetivosEtapa


@dataclass
class ConfigLuz:
    hora_encendido: str = "06:00"  # HH:MM en que empieza el periodo de luz
    umbral_lux: float = 500.0  # por debajo se considera "oscuro"
    minutos_tolerancia: float = 10.0  # retardo antes de avisar de lámpara fundida o fuga de luz


@dataclass
class ConfigHumedad:
    histeresis: float = 3.0  # puntos de % HR de banda muerta
    histeresis_temp: float = 1.0  # °C de banda muerta para el extractor
    tiempo_minimo_ciclo_s: float = 120.0  # protege compresores y relés de encendidos rápidos


@dataclass
class ConfigRiego:
    humedad_suelo_min: float = 35.0  # % por debajo del cual se riega
    duracion_s: float = 20.0  # segundos de bomba por riego
    espera_absorcion_min: float = 45.0  # minutos para que el agua se reparta antes de volver a medir
    max_riegos_dia: int = 6  # límite de seguridad ante sensor o bomba averiados
    solo_con_luz: bool = True  # regar solo durante el periodo de luz


@dataclass
class ConfigNutrientes:
    tolerancia_ec: float = 0.15  # mS/cm por debajo del objetivo antes de dosificar
    margen_ec_alta: float = 0.4  # mS/cm por encima del objetivo para avisar de EC alta
    ml_por_dosis: float = 2.0  # ml de cada parte (A y B) por dosis
    ml_por_dosis_ph: float = 0.5  # ml de regulador de pH por dosis
    caudal_ml_s: float = 1.0  # caudal calibrado de las bombas peristálticas
    espera_mezcla_min: float = 15.0  # minutos entre dosis para que la solución se mezcle
    pausa_entre_partes_s: float = 30.0  # pausa entre la parte A y la B (nunca mezclarlas puras)
    max_dosis_nutrientes_dia: int = 6
    max_dosis_ph_dia: int = 8
    ph_min: float | None = None  # anula el rango del medio de cultivo
    ph_max: float | None = None


@dataclass
class ConfigCalibracion:
    # Sensor capacitivo de humedad del sustrato: voltaje en seco (aire) y en agua.
    suelo_v_seco: float = 2.80
    suelo_v_mojado: float = 1.30
    # Sonda de pH: voltaje medido en las soluciones tampón de pH 7 y pH 4.
    ph_v_7: float = 1.50
    ph_v_4: float = 2.03
    # Sonda de EC: voltaje en agua destilada y en la solución patrón de 1,413 mS/cm.
    ec_v_0: float = 0.0
    ec_v_1413: float = 1.0


@dataclass
class ConfigHardware:
    sensor_clima: str = "sht31"  # "sht31", "dht22" o "ninguno"
    pin_dht: int = 4  # GPIO (BCM) del DHT22
    sensor_luz: bool = True  # BH1750 en el bus I2C
    direccion_ads: int = 0x48  # ADS1115 para las sondas analógicas
    canal_suelo: int = 0  # canal del ADS1115 (-1 = no instalado)
    canal_ec: int = 1
    canal_ph: int = 2
    pin_flotador: int = 17  # GPIO del interruptor de nivel (-1 = no instalado)
    flotador_cerrado_con_agua: bool = True
    reles_activos_en_bajo: bool = True  # la mayoría de placas de relés se activan con nivel bajo
    reles: dict[str, int] = field(
        default_factory=lambda: {
            "luz": 5,
            "bomba_riego": 6,
            "humidificador": 13,
            "deshumidificador": 19,
            "extractor": 26,
            "dosificador_a": 12,
            "dosificador_b": 16,
            "ph_up": 20,
            "ph_down": 21,
        }
    )


@dataclass
class Config:
    etapa: str = "vegetativo"
    medio: str = "coco"
    intervalo_s: float = 60.0  # segundos entre ciclos de control
    registro_csv: str = "registro.csv"
    luz: ConfigLuz = field(default_factory=ConfigLuz)
    humedad: ConfigHumedad = field(default_factory=ConfigHumedad)
    riego: ConfigRiego = field(default_factory=ConfigRiego)
    nutrientes: ConfigNutrientes = field(default_factory=ConfigNutrientes)
    calibracion: ConfigCalibracion = field(default_factory=ConfigCalibracion)
    hardware: ConfigHardware = field(default_factory=ConfigHardware)
    etapas: dict[str, ObjetivosEtapa] = field(default_factory=lambda: dict(ETAPAS))

    @property
    def objetivos(self) -> ObjetivosEtapa:
        return self.etapas[self.etapa]

    @property
    def rango_ph(self) -> tuple[float, float]:
        ph_min, ph_max = RANGOS_PH[self.medio]
        n = self.nutrientes
        return (
            n.ph_min if n.ph_min is not None else ph_min,
            n.ph_max if n.ph_max is not None else ph_max,
        )


_SECCIONES = {
    "luz": ConfigLuz,
    "humedad": ConfigHumedad,
    "riego": ConfigRiego,
    "nutrientes": ConfigNutrientes,
    "calibracion": ConfigCalibracion,
    "hardware": ConfigHardware,
}


def cargar_config(ruta: str | Path | None) -> Config:
    """Lee un archivo TOML; sin ruta devuelve la configuración por defecto."""
    if ruta is None:
        return config_desde_dict({})
    with open(ruta, "rb") as archivo:
        return config_desde_dict(tomllib.load(archivo))


def config_desde_dict(datos: dict[str, Any]) -> Config:
    datos = dict(datos)
    secciones = {
        nombre: _crear(clase, datos.pop(nombre), f"[{nombre}]")
        for nombre, clase in _SECCIONES.items()
        if nombre in datos
    }

    etapas = dict(ETAPAS)
    for nombre, cambios in datos.pop("etapas", {}).items():
        if nombre not in etapas:
            raise ValueError(f"Etapa desconocida en [etapas.{nombre}]; válidas: {', '.join(ETAPAS)}")
        _comprobar_claves(ObjetivosEtapa, cambios, f"[etapas.{nombre}]")
        etapas[nombre] = replace(etapas[nombre], **cambios)

    config = _crear(Config, {**datos, **secciones, "etapas": etapas}, "la raíz del archivo")
    validar(config)
    return config


def parsear_hora(texto: str) -> int:
    """'HH:MM' -> minutos desde medianoche."""
    try:
        horas, minutos = (int(parte) for parte in texto.split(":"))
    except ValueError:
        raise ValueError(f"Hora no válida: {texto!r} (formato HH:MM)") from None
    if not (0 <= horas < 24 and 0 <= minutos < 60):
        raise ValueError(f"Hora no válida: {texto!r} (formato HH:MM)")
    return horas * 60 + minutos


def validar(config: Config) -> None:
    errores = []
    if config.etapa not in config.etapas:
        errores.append(f"etapa '{config.etapa}' no existe; válidas: {', '.join(config.etapas)}")
    if config.medio not in RANGOS_PH:
        errores.append(f"medio '{config.medio}' no existe; válidos: {', '.join(RANGOS_PH)}")
    if config.intervalo_s <= 0:
        errores.append("intervalo_s debe ser mayor que 0")

    try:
        parsear_hora(config.luz.hora_encendido)
    except ValueError as error:
        errores.append(f"[luz] {error}")

    histeresis = config.humedad.histeresis
    for nombre, o in config.etapas.items():
        if not 0 <= o.horas_luz <= 24:
            errores.append(f"[etapas.{nombre}] horas_luz debe estar entre 0 y 24")
        if o.hr_max - o.hr_min < 2 * histeresis:
            errores.append(
                f"[etapas.{nombre}] hr_max - hr_min debe ser al menos 2 × histeresis ({2 * histeresis})"
            )
        if o.ec < 0:
            errores.append(f"[etapas.{nombre}] ec no puede ser negativa")

    r = config.riego
    if not 0 < r.humedad_suelo_min < 100:
        errores.append("[riego] humedad_suelo_min debe estar entre 0 y 100")
    if not 0 < r.duracion_s <= 300:
        errores.append("[riego] duracion_s debe estar entre 0 y 300 segundos")

    n = config.nutrientes
    if n.caudal_ml_s <= 0 or n.ml_por_dosis <= 0 or n.ml_por_dosis_ph <= 0:
        errores.append("[nutrientes] caudal y volúmenes de dosis deben ser mayores que 0")
    if config.medio in RANGOS_PH:
        ph_min, ph_max = config.rango_ph
        if ph_min >= ph_max:
            errores.append("[nutrientes] ph_min debe ser menor que ph_max")

    if config.hardware.sensor_clima not in ("sht31", "dht22", "ninguno"):
        errores.append("[hardware] sensor_clima debe ser 'sht31', 'dht22' o 'ninguno'")

    if errores:
        raise ValueError("Configuración no válida:\n  - " + "\n  - ".join(errores))


def _comprobar_claves(clase: type, valores: dict[str, Any], donde: str) -> None:
    desconocidas = set(valores) - {f.name for f in fields(clase)}
    if desconocidas:
        raise ValueError(f"Claves desconocidas en {donde}: {', '.join(sorted(desconocidas))}")


def _crear(clase: type, valores: dict[str, Any], donde: str):
    _comprobar_claves(clase, valores, donde)
    return clase(**valores)
