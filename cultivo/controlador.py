"""Bucle principal: lee los sensores, decide con cada controlador y acciona los relés."""

from __future__ import annotations

import logging
from collections import Counter
from datetime import datetime

from .config import Config
from .controladores import ControladorHumedad, ControladorLuz, ControladorNutrientes, ControladorRiego
from .hardware import Hardware
from .modelos import Lecturas
from .registro import GestorAlarmas, RegistroCSV

log = logging.getLogger(__name__)

MAX_FALLOS_SEGUIDOS = 5


class ControladorCultivo:
    def __init__(self, config: Config, hardware: Hardware, registro: RegistroCSV | None = None):
        self.config = config
        self.hardware = hardware
        self.registro = registro
        o = config.objetivos
        self.luz = ControladorLuz(config.luz, o.horas_luz)
        self.humedad = ControladorHumedad(config.humedad, o)
        self.riego = ControladorRiego(config.riego)
        self.nutrientes = ControladorNutrientes(config.nutrientes, o.ec, config.rango_ph)
        self.alarmas = GestorAlarmas()
        self.contadores: Counter[str] = Counter()  # pulsos por actuador

    def ciclo(self) -> Lecturas:
        hw = self.hardware
        lecturas = hw.leer()
        eventos: list[str] = []

        def evento(texto: str) -> None:
            log.info(texto)
            eventos.append(texto)

        luz_encendida = self.luz.actualizar(lecturas)
        if luz_encendida != hw.estados()["luz"]:
            evento("Luz encendida" if luz_encendida else "Luz apagada")
        hw.fijar("luz", luz_encendida)
        for actuador, encendido in self.humedad.actualizar(lecturas).items():
            if encendido != hw.estados()[actuador]:
                log.debug("%s %s", actuador, "encendido" if encendido else "apagado")
            hw.fijar(actuador, encendido)

        segundos = self.riego.actualizar(lecturas, luz_encendida)
        if segundos:
            evento(f"Riego {segundos:g} s (sustrato al {lecturas.humedad_suelo:.0f} %)")
            self._pulso("bomba_riego", segundos)

        for i, (actuador, segundos) in enumerate(self.nutrientes.actualizar(lecturas)):
            if i:
                hw.reloj.dormir(self.config.nutrientes.pausa_entre_partes_s)
            evento(f"Dosis {actuador} {segundos:g} s (EC {lecturas.ec:.2f}, pH {lecturas.ph:.2f})")
            self._pulso(actuador, segundos)

        alarmas = [*self.luz.alarmas, *self.humedad.alarmas, *self.riego.alarmas, *self.nutrientes.alarmas]
        if lecturas.deposito_ok is False:
            alarmas.append("Nivel bajo en el depósito: riego y dosificación bloqueados hasta rellenar")
        self.alarmas.actualizar(alarmas)

        if self.registro is not None:
            self.registro.escribir(lecturas, self.config.etapa, hw.estados(), eventos, self.alarmas.activas)
        return lecturas

    def esperar_siguiente_ciclo(self, inicio_ciclo: datetime) -> None:
        """Duerme lo que falte para completar `intervalo_s` desde el inicio del ciclo."""
        intervalo = self.config.intervalo_s
        transcurrido = (self.hardware.reloj.ahora() - inicio_ciclo).total_seconds()
        # Acotado por si el reloj del sistema salta (p. ej. sincronización NTP al arrancar).
        self.hardware.reloj.dormir(min(intervalo, max(0.0, intervalo - transcurrido)))

    def ejecutar(self) -> None:
        """Bucle infinito. Al salir (Ctrl+C, SIGTERM o error grave) deja todo apagado."""
        fallos_seguidos = 0
        try:
            while True:
                inicio = self.hardware.reloj.ahora()
                try:
                    self.ciclo()
                    fallos_seguidos = 0
                except Exception:
                    fallos_seguidos += 1
                    log.exception("Error en el ciclo de control (%d seguidos)", fallos_seguidos)
                    if fallos_seguidos >= MAX_FALLOS_SEGUIDOS:
                        raise
                self.esperar_siguiente_ciclo(inicio)
        finally:
            log.info("Apagando todos los actuadores")
            self.hardware.apagar_todo()

    def _pulso(self, actuador: str, segundos: float) -> None:
        self.contadores[actuador] += 1
        self.hardware.pulso(actuador, segundos)
