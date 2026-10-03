"""Punto de entrada: python -m cultivo [--simular] [--config config.toml] ..."""

from __future__ import annotations

import argparse
import logging
import signal
import sys
from datetime import datetime

from .config import Config, cargar_config
from .controlador import ControladorCultivo
from .hardware import Hardware, Reloj, RelojReal
from .registro import RegistroCSV

log = logging.getLogger("cultivo")


class _FiltroReloj(logging.Filter):
    """Pone en cada línea de log la hora del reloj del controlador (real o simulado)."""

    def __init__(self, reloj: Reloj):
        super().__init__()
        self.reloj = reloj

    def filter(self, record: logging.LogRecord) -> bool:
        record.reloj = self.reloj.ahora().strftime("%Y-%m-%d %H:%M:%S")
        return True


def _configurar_log(reloj: Reloj, detallado: bool) -> None:
    manejador = logging.StreamHandler()
    manejador.addFilter(_FiltroReloj(reloj))
    manejador.setFormatter(logging.Formatter("%(reloj)s %(levelname)-7s %(message)s"))
    logging.basicConfig(level=logging.DEBUG if detallado else logging.INFO, handlers=[manejador])


def _argumentos(argv: list[str] | None) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        prog="python -m cultivo",
        description="Controlador de riego, humedad, luz y nutrientes para cannabis.",
    )
    p.add_argument("-c", "--config", help="archivo TOML de configuración (por defecto, valores de fábrica)")
    p.add_argument("--registro", help="CSV donde guardar el histórico (anula registro_csv)")
    p.add_argument("-v", "--detallado", action="store_true", help="muestra también los cambios de relés")
    modo = p.add_mutually_exclusive_group()
    modo.add_argument("--simular", action="store_true", help="usa el armario virtual en lugar de la Raspberry Pi")
    modo.add_argument("--calibrar", action="store_true", help="muestra los voltajes de las sondas para calibrarlas")
    modo.add_argument("--probar-reles", action="store_true", help="activa cada relé 2 s para comprobar el cableado")
    p.add_argument("--dias", type=float, default=3, help="días a simular (por defecto 3)")
    p.add_argument(
        "--inicio",
        type=datetime.fromisoformat,
        help="fecha y hora de inicio de la simulación, p. ej. 2026-10-03T00:00 (por defecto, hoy a las 00:00)",
    )
    return p.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = _argumentos(argv)
    try:
        config = cargar_config(args.config)
    except (OSError, ValueError) as error:
        print(f"Error en la configuración: {error}", file=sys.stderr)
        return 2

    if args.simular:
        return _simular(config, args)

    from .hardware.raspberry import HardwareRaspberry

    _configurar_log(RelojReal(), args.detallado)
    try:
        hardware = HardwareRaspberry(config)
    except ImportError as error:
        print(
            f"Faltan las librerías de la Raspberry Pi ({error.name}): "
            "pip install -r requirements-raspberry.txt\nPara probar sin hardware usa --simular.",
            file=sys.stderr,
        )
        return 1
    if args.calibrar:
        return _calibrar(hardware)
    if args.probar_reles:
        return _probar_reles(hardware)
    return _ejecutar(hardware, config, args.registro or config.registro_csv)


def _ejecutar(hardware: Hardware, config: Config, ruta_registro: str) -> int:
    # systemd para el servicio con SIGTERM: lo convertimos en salida limpia para apagar los relés.
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
    registro = RegistroCSV(ruta_registro)
    o = config.objetivos
    log.info(
        "Arrancando — etapa %s (%g h de luz desde %s, HR %g–%g %%, EC %g), medio %s, registro en %s",
        config.etapa, o.horas_luz, config.luz.hora_encendido, o.hr_min, o.hr_max, o.ec, config.medio,
        ruta_registro,
    )
    try:
        ControladorCultivo(config, hardware, registro).ejecutar()
    except KeyboardInterrupt:
        pass
    finally:
        registro.cerrar()
    return 0


def _simular(config: Config, args: argparse.Namespace) -> int:
    from .simulacion import crear_simulador, simular

    inicio = args.inicio or datetime.now().replace(hour=0, minute=0, second=0, microsecond=0)
    simulador = crear_simulador(config, inicio)
    _configurar_log(simulador, args.detallado)
    ruta = args.registro or "simulacion.csv"
    registro = RegistroCSV(ruta)
    try:
        resumen = simular(config, args.dias, inicio, registro, simulador)
    finally:
        registro.cerrar()
    print()
    print(resumen.texto(config))
    print(f"  Histórico completo en {ruta}")
    return 0


def _calibrar(hardware) -> int:
    print("Voltajes de las sondas (Ctrl+C para salir). Anota los valores en [calibracion].")
    try:
        while True:
            voltajes = hardware.voltajes()
            lecturas = hardware.leer()
            partes = [f"{nombre}: {v:.3f} V" for nombre, v in voltajes.items() if v is not None]
            if lecturas.ph is not None:
                partes.append(f"→ pH {lecturas.ph:.2f}")
            if lecturas.ec is not None:
                partes.append(f"→ EC {lecturas.ec:.2f} mS/cm")
            if lecturas.humedad_suelo is not None:
                partes.append(f"→ sustrato {lecturas.humedad_suelo:.0f} %")
            print("  ".join(partes))
            hardware.reloj.dormir(2)
    except KeyboardInterrupt:
        return 0


def _probar_reles(hardware: Hardware) -> int:
    try:
        for actuador in hardware.estados():
            print(f"Activando {actuador} durante 2 s...")
            hardware.pulso(actuador, 2)
            hardware.reloj.dormir(1)
    finally:
        hardware.apagar_todo()
    return 0


if __name__ == "__main__":
    sys.exit(main())
