"""Tipos de datos compartidos entre sensores, controladores y hardware."""

from __future__ import annotations

import math
from dataclasses import dataclass
from datetime import datetime

# Todos los relés que maneja el controlador.
ACTUADORES = (
    "luz",
    "bomba_riego",
    "humidificador",
    "deshumidificador",
    "extractor",
    "dosificador_a",
    "dosificador_b",
    "ph_up",
    "ph_down",
)


@dataclass(frozen=True)
class Lecturas:
    """Una lectura de todos los sensores. `None` significa sensor ausente o averiado."""

    ahora: datetime
    temperatura: float | None = None  # °C del aire
    humedad_relativa: float | None = None  # % HR del aire
    humedad_suelo: float | None = None  # % (0 = seco, 100 = saturado)
    lux: float | None = None
    ec: float | None = None  # mS/cm del depósito, compensada a 25 °C
    ph: float | None = None  # pH del depósito
    deposito_ok: bool | None = None  # False = nivel bajo (flotador)


def calcular_vpd(temperatura: float | None, humedad_relativa: float | None) -> float | None:
    """Déficit de presión de vapor del aire en kPa (fórmula de Tetens)."""
    if temperatura is None or humedad_relativa is None:
        return None
    presion_saturacion = 0.6108 * math.exp(17.27 * temperatura / (temperatura + 237.3))
    return presion_saturacion * (1 - humedad_relativa / 100)
