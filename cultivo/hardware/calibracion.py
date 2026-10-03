"""Conversión de voltajes de las sondas analógicas a unidades físicas."""

from __future__ import annotations

from ..config import ConfigCalibracion

COEF_TEMPERATURA_EC = 0.02  # la EC sube ~2 % por °C


def humedad_suelo(voltaje: float, cal: ConfigCalibracion) -> float:
    """Sensor capacitivo: más agua = menos voltaje. Devuelve 0–100 %."""
    porcentaje = 100 * (cal.suelo_v_seco - voltaje) / (cal.suelo_v_seco - cal.suelo_v_mojado)
    return min(100.0, max(0.0, porcentaje))


def ph(voltaje: float, cal: ConfigCalibracion) -> float:
    """Recta que pasa por los dos puntos de calibración (pH 7 y pH 4)."""
    pendiente = (7.0 - 4.0) / (cal.ph_v_7 - cal.ph_v_4)
    return 7.0 + (voltaje - cal.ph_v_7) * pendiente


def ec(voltaje: float, cal: ConfigCalibracion, temperatura: float | None) -> float:
    """EC en mS/cm compensada a 25 °C."""
    ec_medida = 1.413 * (voltaje - cal.ec_v_0) / (cal.ec_v_1413 - cal.ec_v_0)
    if temperatura is not None:
        ec_medida /= 1 + COEF_TEMPERATURA_EC * (temperatura - 25.0)
    return max(0.0, ec_medida)
