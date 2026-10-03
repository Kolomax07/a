"""Riego por humedad del sustrato."""

from __future__ import annotations

from datetime import date, datetime, timedelta

from ..config import ConfigRiego
from ..modelos import Lecturas


class ControladorRiego:
    """Decide cuántos segundos debe funcionar la bomba de riego en este ciclo (0 = no regar).

    Riega cuando el sustrato baja de `humedad_suelo_min`, deja un tiempo de absorción
    antes de volver a regar (el sensor tarda en notar el agua) y nunca supera
    `max_riegos_dia`, para que un sensor averiado no encharque las raíces.
    """

    def __init__(self, config: ConfigRiego):
        self.config = config
        self._ultimo_riego: datetime | None = None
        self._dia: date | None = None
        self.riegos_hoy = 0
        self.alarmas: list[str] = []

    def actualizar(self, lecturas: Lecturas, luz_encendida: bool) -> float:
        self.alarmas = []
        ahora = lecturas.ahora
        if self._dia != ahora.date():
            self._dia = ahora.date()
            self.riegos_hoy = 0

        suelo = lecturas.humedad_suelo
        if suelo is None:
            self.alarmas.append("Sin lectura de humedad del sustrato: riego en pausa")
            return 0.0
        if suelo >= self.config.humedad_suelo_min:
            return 0.0
        espera = timedelta(minutes=self.config.espera_absorcion_min)
        if self._ultimo_riego is not None and ahora - self._ultimo_riego < espera:
            return 0.0
        if self.config.solo_con_luz and not luz_encendida:
            return 0.0
        if lecturas.deposito_ok is False:
            return 0.0  # el aviso de depósito bajo lo da el controlador principal
        if self.riegos_hoy >= self.config.max_riegos_dia:
            self.alarmas.append(
                "Límite diario de riegos alcanzado y el sustrato sigue seco: revisa bomba, goteros y sensor"
            )
            return 0.0

        self._ultimo_riego = ahora
        self.riegos_hoy += 1
        return self.config.duracion_s
