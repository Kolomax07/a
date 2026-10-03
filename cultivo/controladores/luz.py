"""Fotoperiodo de la lámpara y vigilancia del sensor de luz."""

from __future__ import annotations

from datetime import datetime, timedelta

from ..config import ConfigLuz, parsear_hora
from ..modelos import Lecturas

MINUTOS_DIA = 24 * 60


class ControladorLuz:
    """Enciende la lámpara `horas_luz` horas seguidas a partir de `hora_encendido`.

    El periodo puede cruzar la medianoche (p. ej. 18:00 → 06:00). Si hay sensor de
    luz, avisa cuando la lámpara no responde o cuando entra luz en el periodo de
    oscuridad, que en floración puede provocar plantas hermafroditas.
    """

    def __init__(self, config: ConfigLuz, horas_luz: float):
        self.config = config
        self.horas_luz = horas_luz
        self._minuto_encendido = parsear_hora(config.hora_encendido)
        self._discrepancia_desde: datetime | None = None
        self.alarmas: list[str] = []

    def debe_estar_encendida(self, ahora: datetime) -> bool:
        minuto_del_dia = ahora.hour * 60 + ahora.minute + ahora.second / 60
        transcurrido = (minuto_del_dia - self._minuto_encendido) % MINUTOS_DIA
        return transcurrido < self.horas_luz * 60

    def actualizar(self, lecturas: Lecturas) -> bool:
        self.alarmas = []
        encendida = self.debe_estar_encendida(lecturas.ahora)
        self._vigilar_sensor(lecturas, encendida)
        return encendida

    def _vigilar_sensor(self, lecturas: Lecturas, encendida: bool) -> None:
        if lecturas.lux is None:
            self._discrepancia_desde = None
            return
        hay_luz = lecturas.lux >= self.config.umbral_lux
        if hay_luz == encendida:
            self._discrepancia_desde = None
            return

        if self._discrepancia_desde is None:
            self._discrepancia_desde = lecturas.ahora
        tolerancia = timedelta(minutes=self.config.minutos_tolerancia)
        if lecturas.ahora - self._discrepancia_desde < tolerancia:
            return
        if encendida:
            self.alarmas.append(
                "La lámpara debería estar encendida pero no se detecta luz: revisa lámpara, driver y relé"
            )
        else:
            self.alarmas.append(
                "Entra luz durante el periodo de oscuridad: revisa fugas de luz (en floración "
                "pueden provocar plantas hermafroditas)"
            )
