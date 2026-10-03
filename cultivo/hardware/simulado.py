"""Armario de cultivo virtual para probar el controlador sin hardware.

El modelo es deliberadamente simple (ecuaciones de primer orden), pero reproduce
lo que importa para el control: la lámpara calienta y la planta transpira, el
sustrato se seca, la EC baja y el pH sube con el tiempo, y cada actuador empuja
su variable en la dirección esperada.
"""

from __future__ import annotations

import random
from datetime import datetime, timedelta

from ..modelos import Lecturas
from .base import Hardware

TEMP_AMBIENTE = 21.0  # °C del aire que entra por el extractor
HR_AMBIENTE = 45.0  # % HR del aire que entra por el extractor
CAUDAL_RIEGO_ML_S = 25.0
VOLUMEN_MINIMO_L = 5.0  # por debajo el flotador marca nivel bajo


class Simulador(Hardware):
    def __init__(
        self,
        inicio: datetime,
        caudal_dosificadoras_ml_s: float = 1.0,
        semilla: int | None = 1,
        paso_s: float = 10.0,
    ):
        super().__init__()
        self.reloj = self
        self._ahora = inicio
        self._paso_s = paso_s
        self._caudal_dosificadoras = caudal_dosificadoras_ml_s
        self._rng = random.Random(semilla)

        # Estado físico
        self.temperatura = 22.0
        self.humedad_relativa = 55.0
        self.humedad_suelo = 45.0
        self.volumen_l = 40.0
        self.ec = 0.9
        self.ph = 6.5

        # Averías inyectables para probar alarmas
        self.sensores_averiados: set[str] = set()
        self.lampara_fundida = False

    # --- Reloj ---------------------------------------------------------------

    def ahora(self) -> datetime:
        return self._ahora

    def dormir(self, segundos: float) -> None:
        restante = segundos
        while restante > 0:
            dt = min(self._paso_s, restante)
            self._integrar(dt)
            self._ahora += timedelta(seconds=dt)
            restante -= dt

    # --- Hardware ------------------------------------------------------------

    def _escribir_rele(self, actuador: str, encendido: bool) -> None:
        pass  # el estado ya lo guarda la clase base y lo lee _integrar

    def leer(self) -> Lecturas:
        ruido = self._rng.gauss
        valores = {
            "temperatura": self.temperatura + ruido(0, 0.1),
            "humedad_relativa": self.humedad_relativa + ruido(0, 0.5),
            "humedad_suelo": self.humedad_suelo + ruido(0, 0.5),
            "lux": max(0.0, (30000.0 if self._lampara_da_luz() else 0.0) + ruido(0, 20)),
            "ec": max(0.0, self.ec + ruido(0, 0.01)),
            "ph": self.ph + ruido(0, 0.01),
            "deposito_ok": self.volumen_l > VOLUMEN_MINIMO_L,
        }
        for sensor in self.sensores_averiados:
            valores[sensor] = None
        return Lecturas(ahora=self._ahora, **valores)

    # --- Física --------------------------------------------------------------

    def _lampara_da_luz(self) -> bool:
        return self._estados["luz"] and not self.lampara_fundida

    def _integrar(self, dt: float) -> None:
        e = self._estados
        horas = dt / 3600
        luz = self._lampara_da_luz()

        # Temperatura: la lámpara calienta, el extractor renueva el aire.
        if e["extractor"]:
            objetivo, k = TEMP_AMBIENTE + (3.0 if luz else 0.0), 3.0
        else:
            objetivo, k = TEMP_AMBIENTE + (8.0 if luz else 0.0), 1.0
        self.temperatura += (objetivo - self.temperatura) * min(1.0, k * horas)

        # Humedad relativa: transpiración, fugas, extractor y equipos.
        transpiracion = 6.0 if luz else 2.0
        renovacion = 0.5 + (4.0 if e["extractor"] else 0.0)
        cambio = transpiracion - renovacion * (self.humedad_relativa - HR_AMBIENTE)
        cambio += 40.0 if e["humidificador"] else 0.0
        cambio -= 40.0 if e["deshumidificador"] else 0.0
        self.humedad_relativa = min(99.0, max(15.0, self.humedad_relativa + cambio * horas))

        # Sustrato: se seca más con luz; la bomba lo moja desde el depósito.
        self.humedad_suelo -= (2.0 if luz else 0.6) * horas
        if e["bomba_riego"] and self.volumen_l > 0:
            self.humedad_suelo += 0.8 * dt
            self.volumen_l -= CAUDAL_RIEGO_ML_S * dt / 1000
        self.humedad_suelo = min(100.0, max(0.0, self.humedad_suelo))

        # Depósito: la planta consume nutrientes y el pH deriva hacia arriba.
        self.ec = max(0.0, self.ec - 0.005 * horas)
        self.ph += 0.01 * horas
        ml = self._caudal_dosificadoras * dt
        litros = max(self.volumen_l, 1.0)
        for parte in ("dosificador_a", "dosificador_b"):
            if e[parte]:
                self.ec += 0.5 * ml / litros
                self.ph -= 0.4 * ml / litros
        if e["ph_down"]:
            self.ph -= 4.0 * ml / litros
        if e["ph_up"]:
            self.ph += 4.0 * ml / litros
