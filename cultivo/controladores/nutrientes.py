"""Dosificación de nutrientes (EC) y corrección de pH del depósito."""

from __future__ import annotations

from datetime import date, datetime, timedelta

from ..config import ConfigNutrientes
from ..modelos import Lecturas

# Fuera de estos rangos la lectura es físicamente imposible en un depósito de riego:
# la sonda está averiada, descalibrada o fuera del agua.
RANGO_EC_VALIDO = (0.0, 6.0)
RANGO_PH_VALIDO = (2.0, 12.0)


class ControladorNutrientes:
    """Mantiene la EC y el pH del depósito con pequeñas dosis y esperas de mezcla.

    En cada ciclo hace como mucho UNA corrección:
    1. Si la EC baja de `objetivo - tolerancia`, dosifica nutriente A y después B
       hasta volver al objetivo (el pH se corrige después, porque los nutrientes lo cambian).
    2. Si la EC está bien y el pH sale del rango, lo corrige con pH- o pH+ hasta
       el centro del rango, para no estar dosificando siempre en el borde.
    Tras cada dosis espera `espera_mezcla_min` antes de volver a medir. Los límites
    diarios evitan vaciar los bidones en el depósito si una sonda se estropea.
    """

    def __init__(self, config: ConfigNutrientes, ec_objetivo: float, rango_ph: tuple[float, float]):
        self.config = config
        self.ec_objetivo = ec_objetivo
        self.rango_ph = rango_ph
        self._subiendo_ec = False
        self._corrector_ph: str | None = None  # "ph_down" o "ph_up" mientras se corrige
        self._ultima_dosis: datetime | None = None
        self._dia: date | None = None
        self.dosis_nutrientes_hoy = 0
        self.dosis_ph_hoy = 0
        self.alarmas: list[str] = []

    def actualizar(self, lecturas: Lecturas) -> list[tuple[str, float]]:
        """Devuelve la lista de (actuador, segundos) a ejecutar en orden."""
        self.alarmas = []
        ahora = lecturas.ahora
        if self._dia != ahora.date():
            self._dia = ahora.date()
            self.dosis_nutrientes_hoy = 0
            self.dosis_ph_hoy = 0

        if lecturas.deposito_ok is False:
            return []  # con nivel bajo las sondas pueden estar fuera del agua
        ec, ph = lecturas.ec, lecturas.ph
        if ec is None or not RANGO_EC_VALIDO[0] <= ec <= RANGO_EC_VALIDO[1]:
            self.alarmas.append("Lectura de EC no válida: dosificación en pausa, revisa la sonda")
            return []
        if ph is None or not RANGO_PH_VALIDO[0] <= ph <= RANGO_PH_VALIDO[1]:
            self.alarmas.append("Lectura de pH no válida: dosificación en pausa, revisa la sonda")
            return []

        c = self.config
        maneja_ec = self.ec_objetivo > 0
        if maneja_ec and ec > self.ec_objetivo + c.margen_ec_alta:
            self.alarmas.append("EC alta en el depósito: añade agua sin nutrientes")

        if self._ultima_dosis is not None and ahora - self._ultima_dosis < timedelta(minutes=c.espera_mezcla_min):
            return []

        if ec < self.ec_objetivo - c.tolerancia_ec:
            self._subiendo_ec = maneja_ec
        elif ec >= self.ec_objetivo:
            self._subiendo_ec = False

        ph_min, ph_max = self.rango_ph
        centro = (ph_min + ph_max) / 2
        if ph > ph_max:
            self._corrector_ph = "ph_down"
        elif ph < ph_min:
            self._corrector_ph = "ph_up"
        elif (self._corrector_ph == "ph_down" and ph <= centro) or (self._corrector_ph == "ph_up" and ph >= centro):
            self._corrector_ph = None

        if self._subiendo_ec:
            if self.dosis_nutrientes_hoy < c.max_dosis_nutrientes_dia:
                self.dosis_nutrientes_hoy += 1
                self._ultima_dosis = ahora
                segundos = c.ml_por_dosis / c.caudal_ml_s
                return [("dosificador_a", segundos), ("dosificador_b", segundos)]
            if ec < self.ec_objetivo - c.tolerancia_ec:
                self.alarmas.append(
                    "Límite diario de dosis de nutrientes alcanzado y la EC sigue baja: "
                    "revisa la sonda de EC y los bidones"
                )
            # sin dosis de nutrientes disponibles, al menos se mantiene el pH

        if self._corrector_ph is None:
            return []
        if self.dosis_ph_hoy >= c.max_dosis_ph_dia:
            if not ph_min <= ph <= ph_max:
                self.alarmas.append(
                    "Límite diario de correcciones de pH alcanzado y sigue fuera de rango: "
                    "revisa la sonda de pH y los reguladores"
                )
            return []
        self.dosis_ph_hoy += 1
        self._ultima_dosis = ahora
        return [(self._corrector_ph, c.ml_por_dosis_ph / c.caudal_ml_s)]
