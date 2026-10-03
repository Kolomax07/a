"""Interfaz común del hardware real y del simulado."""

from __future__ import annotations

import logging
import time
from abc import ABC, abstractmethod
from datetime import datetime
from typing import Protocol

from ..modelos import ACTUADORES, Lecturas

log = logging.getLogger(__name__)


class Reloj(Protocol):
    def ahora(self) -> datetime: ...

    def dormir(self, segundos: float) -> None: ...


class RelojReal:
    def ahora(self) -> datetime:
        return datetime.now()

    def dormir(self, segundos: float) -> None:
        time.sleep(segundos)


class Hardware(ABC):
    """Sensores + relés. Las subclases implementan `leer` y `_escribir_rele`."""

    reloj: Reloj

    def __init__(self) -> None:
        self._estados = dict.fromkeys(ACTUADORES, False)

    @abstractmethod
    def leer(self) -> Lecturas: ...

    @abstractmethod
    def _escribir_rele(self, actuador: str, encendido: bool) -> None: ...

    def fijar(self, actuador: str, encendido: bool) -> None:
        if actuador not in self._estados:
            raise KeyError(f"Actuador desconocido: {actuador}")
        # Se escribe siempre (no solo al cambiar) para reafirmar el estado del relé.
        self._escribir_rele(actuador, encendido)
        if self._estados[actuador] != encendido:
            log.debug("%s -> %s", actuador, "ON" if encendido else "OFF")
            self._estados[actuador] = encendido

    def pulso(self, actuador: str, segundos: float) -> None:
        """Enciende un actuador durante `segundos` y garantiza que queda apagado."""
        self.fijar(actuador, True)
        try:
            self.reloj.dormir(segundos)
        finally:
            self.fijar(actuador, False)

    def apagar_todo(self) -> None:
        for actuador in ACTUADORES:
            try:
                self.fijar(actuador, False)
            except Exception:  # seguir apagando el resto aunque falle uno
                log.exception("No se pudo apagar %s", actuador)

    def estados(self) -> dict[str, bool]:
        return dict(self._estados)
