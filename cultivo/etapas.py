"""Objetivos ambientales por etapa de cultivo.

Son valores de partida habituales; ajústalos a tu genética y a la tabla de tu
marca de fertilizantes desde la sección [etapas.<nombre>] del archivo de configuración.
"""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class ObjetivosEtapa:
    horas_luz: float  # horas de luz por cada 24 h
    hr_min: float  # % humedad relativa mínima
    hr_max: float  # % humedad relativa máxima
    temp_max: float  # °C a partir de los cuales se enciende el extractor
    ec: float  # mS/cm objetivo del depósito (0 = no dosificar nutrientes)


ETAPAS: dict[str, ObjetivosEtapa] = {
    "plantula": ObjetivosEtapa(horas_luz=18, hr_min=65, hr_max=75, temp_max=27, ec=0.6),
    "vegetativo": ObjetivosEtapa(horas_luz=18, hr_min=50, hr_max=65, temp_max=28, ec=1.2),
    "floracion": ObjetivosEtapa(horas_luz=12, hr_min=40, hr_max=50, temp_max=26, ec=1.6),
    "floracion_tardia": ObjetivosEtapa(horas_luz=12, hr_min=35, hr_max=45, temp_max=25, ec=1.4),
    # Últimos días antes de cosechar: solo agua con el pH corregido.
    "lavado": ObjetivosEtapa(horas_luz=12, hr_min=35, hr_max=45, temp_max=25, ec=0.0),
}

# Rango de pH de la solución de riego según el medio de cultivo.
RANGOS_PH: dict[str, tuple[float, float]] = {
    "tierra": (6.2, 6.8),
    "coco": (5.7, 6.3),
    "hidro": (5.6, 6.2),
}
