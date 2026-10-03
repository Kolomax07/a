"""Ejecuta el controlador contra el armario virtual y resume cómo se ha comportado."""

from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime, timedelta

from .config import Config
from .controlador import ControladorCultivo
from .hardware.simulado import Simulador
from .registro import RegistroCSV


@dataclass
class Resumen:
    dias: float
    pct_hr_en_rango: float
    hr_min: float
    hr_max: float
    temp_min: float
    temp_max: float
    suelo_min: float
    riegos: int
    dosis_nutrientes: int
    dosis_ph: int
    ec_final: float
    ph_final: float
    alarmas: set[str] = field(default_factory=set)

    def texto(self, config: Config) -> str:
        o = config.objetivos
        ph_min, ph_max = config.rango_ph
        lineas = [
            f"Simulación de {self.dias:g} días — etapa {config.etapa}, medio {config.medio}",
            f"  Humedad relativa: {self.pct_hr_en_rango:.0f} % del tiempo en {o.hr_min:g}–{o.hr_max:g} %"
            f" (mín {self.hr_min:.1f}, máx {self.hr_max:.1f})",
            f"  Temperatura: {self.temp_min:.1f}–{self.temp_max:.1f} °C (extractor a partir de {o.temp_max:g} °C)",
            f"  Sustrato: mínimo {self.suelo_min:.1f} % (riega por debajo de "
            f"{config.riego.humedad_suelo_min:g} %), {self.riegos} riegos",
            f"  EC final {self.ec_final:.2f} mS/cm (objetivo {o.ec:g}), {self.dosis_nutrientes} dosis de nutrientes",
            f"  pH final {self.ph_final:.2f} (rango {ph_min:g}–{ph_max:g}), {self.dosis_ph} correcciones",
            "  Alarmas: " + ("; ".join(sorted(self.alarmas)) if self.alarmas else "ninguna"),
        ]
        return "\n".join(lineas)


def crear_simulador(config: Config, inicio: datetime) -> Simulador:
    """Armario virtual con el depósito recién preparado, algo por debajo de la EC objetivo y con pH alto."""
    sim = Simulador(inicio, caudal_dosificadoras_ml_s=config.nutrientes.caudal_ml_s)
    o = config.objetivos
    sim.ec = o.ec - 0.3 if o.ec > 0 else 0.4  # en lavado, agua del grifo
    sim.ph = config.rango_ph[1] + 0.1
    return sim


def simular(
    config: Config,
    dias: float,
    inicio: datetime,
    registro: RegistroCSV | None = None,
    simulador: Simulador | None = None,
) -> Resumen:
    sim = simulador or crear_simulador(config, inicio)
    controlador = ControladorCultivo(config, sim, registro)
    fin = sim.ahora() + timedelta(days=dias)
    o = config.objetivos

    humedades, temperaturas, suelos = [], [], []
    while sim.ahora() < fin:
        inicio_ciclo = sim.ahora()
        lecturas = controlador.ciclo()
        if lecturas.humedad_relativa is not None:
            humedades.append(lecturas.humedad_relativa)
        if lecturas.temperatura is not None:
            temperaturas.append(lecturas.temperatura)
        if lecturas.humedad_suelo is not None:
            suelos.append(lecturas.humedad_suelo)
        controlador.esperar_siguiente_ciclo(inicio_ciclo)
    sim.apagar_todo()

    c = controlador.contadores
    en_rango = sum(o.hr_min <= hr <= o.hr_max for hr in humedades)
    return Resumen(
        dias=dias,
        pct_hr_en_rango=100 * en_rango / max(1, len(humedades)),
        hr_min=min(humedades, default=float("nan")),
        hr_max=max(humedades, default=float("nan")),
        temp_min=min(temperaturas, default=float("nan")),
        temp_max=max(temperaturas, default=float("nan")),
        suelo_min=min(suelos, default=float("nan")),
        riegos=c["bomba_riego"],
        dosis_nutrientes=c["dosificador_a"],
        dosis_ph=c["ph_up"] + c["ph_down"],
        ec_final=sim.ec,
        ph_final=sim.ph,
        alarmas=set(controlador.alarmas.historial),
    )
