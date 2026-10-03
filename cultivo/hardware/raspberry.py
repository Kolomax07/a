"""Hardware real: Raspberry Pi + placa de relés + ADS1115 + SHT31/DHT22 + BH1750 + flotador.

Las librerías se importan aquí dentro para que el resto del proyecto (simulación,
tests) funcione en cualquier ordenador sin tenerlas instaladas.
"""

from __future__ import annotations

import logging
import time
from typing import Any, Callable

from ..config import Config
from ..modelos import ACTUADORES, Lecturas
from . import calibracion
from .base import Hardware, RelojReal

log = logging.getLogger(__name__)


class HardwareRaspberry(Hardware):
    def __init__(self, config: Config):
        super().__init__()
        import board
        from gpiozero import DigitalInputDevice, OutputDevice

        self.reloj = RelojReal()
        hw = self._hw = config.hardware
        self._cal = config.calibracion
        i2c = board.I2C()

        self._reles = {}
        for actuador, pin in hw.reles.items():
            if actuador not in ACTUADORES:
                raise ValueError(f"[hardware.reles] actuador desconocido: {actuador}")
            self._reles[actuador] = OutputDevice(
                pin, active_high=not hw.reles_activos_en_bajo, initial_value=False
            )
        sin_rele = [a for a in ACTUADORES if a not in self._reles]
        if sin_rele:
            log.warning("Actuadores sin relé configurado (se ignorarán): %s", ", ".join(sin_rele))

        self._clima: Callable[[], tuple[float | None, float | None]] | None = None
        if hw.sensor_clima == "sht31":
            import adafruit_sht31d

            sht = adafruit_sht31d.SHT31D(i2c)
            self._clima = lambda: (sht.temperature, sht.relative_humidity)
        elif hw.sensor_clima == "dht22":
            import adafruit_dht

            dht = adafruit_dht.DHT22(getattr(board, f"D{hw.pin_dht}"))
            self._clima = lambda: (dht.temperature, dht.humidity)

        self._luxometro = None
        if hw.sensor_luz:
            import adafruit_bh1750

            self._luxometro = adafruit_bh1750.BH1750(i2c)

        self._canales: dict[str, Any] = {}
        canales = {"suelo": hw.canal_suelo, "ec": hw.canal_ec, "ph": hw.canal_ph}
        if any(n >= 0 for n in canales.values()):
            from adafruit_ads1x15 import ADS1115, AnalogIn, ads1x15

            ads = ADS1115(i2c, address=hw.direccion_ads)
            pines = [ads1x15.Pin.A0, ads1x15.Pin.A1, ads1x15.Pin.A2, ads1x15.Pin.A3]
            self._canales = {nombre: AnalogIn(ads, pines[n]) for nombre, n in canales.items() if n >= 0}

        self._flotador = DigitalInputDevice(hw.pin_flotador, pull_up=True) if hw.pin_flotador >= 0 else None

    def _escribir_rele(self, actuador: str, encendido: bool) -> None:
        rele = self._reles.get(actuador)
        if rele is None:
            return
        if encendido:
            rele.on()
        else:
            rele.off()

    def leer(self) -> Lecturas:
        temperatura = humedad_relativa = None
        if self._clima is not None:
            # El DHT22 falla a menudo de forma puntual; necesita 2 s entre lecturas.
            clima = _leer("temperatura/humedad", self._clima, reintentos=3)
            if clima is not None:
                temperatura, humedad_relativa = clima

        lux = _leer("luz", lambda: self._luxometro.lux) if self._luxometro else None

        cal = self._cal
        v_suelo, v_ec, v_ph = (self._voltaje(nombre) for nombre in ("suelo", "ec", "ph"))
        deposito_ok = None
        if self._flotador is not None:
            deposito_ok = self._flotador.is_active == self._hw.flotador_cerrado_con_agua

        return Lecturas(
            ahora=self.reloj.ahora(),
            temperatura=temperatura,
            humedad_relativa=humedad_relativa,
            humedad_suelo=None if v_suelo is None else calibracion.humedad_suelo(v_suelo, cal),
            lux=lux,
            # Sin sonda de temperatura en el agua se usa la del aire para compensar la EC.
            ec=None if v_ec is None else calibracion.ec(v_ec, cal, temperatura),
            ph=None if v_ph is None else calibracion.ph(v_ph, cal),
            deposito_ok=deposito_ok,
        )

    def voltajes(self) -> dict[str, float | None]:
        """Voltajes en bruto de las sondas analógicas, para calibrar."""
        return {nombre: self._voltaje(nombre) for nombre in self._canales}

    def _voltaje(self, nombre: str) -> float | None:
        canal = self._canales.get(nombre)
        return None if canal is None else _leer(f"sonda {nombre}", lambda: canal.voltage)


def _leer(nombre: str, funcion: Callable[[], Any], reintentos: int = 1) -> Any:
    """Lee un sensor; ante error devuelve None en vez de tumbar el controlador."""
    error: Exception | None = None
    for intento in range(reintentos):
        try:
            valor = funcion()
        except Exception as e:  # los sensores I2C y DHT fallan de forma intermitente
            error = e
        else:
            if valor is not None and not (isinstance(valor, tuple) and None in valor):
                return valor
            error = RuntimeError("lectura vacía")
        if intento + 1 < reintentos:
            time.sleep(2.0)
    log.warning("Error leyendo %s: %s", nombre, error)
    return None
