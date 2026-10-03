"""Control de humedad relativa (humidificador, deshumidificador) y extractor."""

from __future__ import annotations

from datetime import datetime, timedelta

from ..config import ConfigHumedad
from ..etapas import ObjetivosEtapa
from ..modelos import Lecturas

MARGEN_TEMPERATURA_CRITICA = 3.0  # °C sobre temp_max para avisar


class Interruptor:
    """Estado on/off que respeta un tiempo mínimo entre cambios para no castigar los equipos."""

    def __init__(self, tiempo_minimo_s: float):
        self.tiempo_minimo = timedelta(seconds=tiempo_minimo_s)
        self.encendido = False
        self._ultimo_cambio: datetime | None = None

    def pedir(self, ahora: datetime, encendido: bool) -> bool:
        if encendido != self.encendido and (
            self._ultimo_cambio is None or ahora - self._ultimo_cambio >= self.tiempo_minimo
        ):
            self.encendido = encendido
            self._ultimo_cambio = ahora
        return self.encendido

    def apagar(self, ahora: datetime) -> None:
        """Apagado inmediato por seguridad: ignora el tiempo mínimo."""
        if self.encendido:
            self.encendido = False
            self._ultimo_cambio = ahora


def _con_histeresis(encendido: bool, encender: bool, apagar: bool) -> bool:
    if encender:
        return True
    if apagar:
        return False
    return encendido


class ControladorHumedad:
    """Mantiene la HR entre `hr_min` y `hr_max` de la etapa con banda muerta.

    - Humidificador: se enciende por debajo de hr_min y se apaga al llegar a hr_min + histéresis.
    - Deshumidificador: se enciende por encima de hr_max y se apaga al bajar a hr_max - histéresis.
    - Nunca están encendidos a la vez.
    - Extractor: ayuda cuando sobra humedad o calor; si fallan los sensores queda
      encendido, que es el estado más seguro para las plantas.
    """

    def __init__(self, config: ConfigHumedad, objetivos: ObjetivosEtapa):
        self.config = config
        self.objetivos = objetivos
        self.humidificador = Interruptor(config.tiempo_minimo_ciclo_s)
        self.deshumidificador = Interruptor(config.tiempo_minimo_ciclo_s)
        self.extractor = Interruptor(config.tiempo_minimo_ciclo_s)
        self.alarmas: list[str] = []

    def actualizar(self, lecturas: Lecturas) -> dict[str, bool]:
        self.alarmas = []
        ahora = lecturas.ahora
        hr = lecturas.humedad_relativa
        temperatura = lecturas.temperatura
        o = self.objetivos
        h = self.config.histeresis

        if hr is None:
            self.alarmas.append("Sin lectura de humedad relativa: humidificador y deshumidificador apagados")
            self.humidificador.apagar(ahora)
            self.deshumidificador.apagar(ahora)
        else:
            quiere_humedad = _con_histeresis(self.humidificador.encendido, hr < o.hr_min, hr >= o.hr_min + h)
            quiere_secar = _con_histeresis(self.deshumidificador.encendido, hr > o.hr_max, hr <= o.hr_max - h)
            self.humidificador.pedir(ahora, quiere_humedad and not self.deshumidificador.encendido)
            self.deshumidificador.pedir(ahora, quiere_secar and not self.humidificador.encendido)

        if temperatura is None:
            self.alarmas.append("Sin lectura de temperatura: extractor encendido por seguridad")
            self.extractor.pedir(ahora, True)
        else:
            ht = self.config.histeresis_temp
            sobra_calor = temperatura > o.temp_max
            calor_ok = temperatura <= o.temp_max - ht
            sobra_humedad = hr is not None and hr > o.hr_max
            humedad_ok = hr is None or hr <= o.hr_max - h
            self.extractor.pedir(
                ahora,
                _con_histeresis(self.extractor.encendido, sobra_calor or sobra_humedad, calor_ok and humedad_ok),
            )
            if temperatura >= o.temp_max + MARGEN_TEMPERATURA_CRITICA:
                self.alarmas.append("Temperatura crítica: el extractor no da abasto, revisa la ventilación")

        return {
            "humidificador": self.humidificador.encendido,
            "deshumidificador": self.deshumidificador.encendido,
            "extractor": self.extractor.encendido,
        }
