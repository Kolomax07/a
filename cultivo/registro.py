"""Histórico en CSV (abrible con Excel, LibreOffice o pandas) y gestión de alarmas."""

from __future__ import annotations

import csv
import logging
from pathlib import Path

from .modelos import Lecturas, calcular_vpd

log = logging.getLogger(__name__)


class RegistroCSV:
    CAMPOS = [
        "fecha",
        "etapa",
        "temperatura_c",
        "humedad_relativa",
        "vpd_kpa",
        "humedad_suelo",
        "lux",
        "ec",
        "ph",
        "deposito_ok",
        "luz",
        "humidificador",
        "deshumidificador",
        "extractor",
        "eventos",
        "alarmas",
    ]

    def __init__(self, ruta: str | Path):
        ruta = Path(ruta)
        nuevo = not ruta.exists() or ruta.stat().st_size == 0
        self._archivo = open(ruta, "a", newline="", encoding="utf-8")
        self._csv = csv.writer(self._archivo)
        if nuevo:
            self._csv.writerow(self.CAMPOS)

    def escribir(
        self,
        lecturas: Lecturas,
        etapa: str,
        estados: dict[str, bool],
        eventos: list[str],
        alarmas: set[str],
    ) -> None:
        self._csv.writerow(
            [
                lecturas.ahora.isoformat(timespec="seconds"),
                etapa,
                _num(lecturas.temperatura, 1),
                _num(lecturas.humedad_relativa, 1),
                _num(calcular_vpd(lecturas.temperatura, lecturas.humedad_relativa), 2),
                _num(lecturas.humedad_suelo, 1),
                _num(lecturas.lux, 0),
                _num(lecturas.ec, 2),
                _num(lecturas.ph, 2),
                "" if lecturas.deposito_ok is None else int(lecturas.deposito_ok),
                *(int(estados[nombre]) for nombre in ("luz", "humidificador", "deshumidificador", "extractor")),
                "; ".join(eventos),
                "; ".join(sorted(alarmas)),
            ]
        )
        self._archivo.flush()

    def cerrar(self) -> None:
        self._archivo.close()


def _num(valor: float | None, decimales: int) -> str:
    return "" if valor is None else f"{valor:.{decimales}f}"


class GestorAlarmas:
    """Registra cada alarma una sola vez al aparecer y otra al resolverse."""

    def __init__(self) -> None:
        self.activas: set[str] = set()
        self.historial: set[str] = set()

    def actualizar(self, alarmas: list[str]) -> None:
        nuevas = set(alarmas)
        for alarma in sorted(nuevas - self.activas):
            log.warning("ALARMA: %s", alarma)
        for alarma in sorted(self.activas - nuevas):
            log.info("Resuelta: %s", alarma)
        self.activas = nuevas
        self.historial |= nuevas
