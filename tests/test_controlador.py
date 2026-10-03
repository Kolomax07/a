"""Pruebas de extremo a extremo contra el armario virtual."""

import csv
import tempfile
import unittest
from datetime import timedelta
from pathlib import Path

from cultivo.config import ConfigCalibracion, cargar_config, config_desde_dict
from cultivo.controlador import ControladorCultivo
from cultivo.hardware import calibracion
from cultivo.hardware.simulado import Simulador
from cultivo.modelos import ACTUADORES, calcular_vpd
from cultivo.registro import RegistroCSV
from cultivo.simulacion import crear_simulador, simular

from .utilidades import INICIO


class TestSimulacion(unittest.TestCase):
    def test_tres_dias_de_vegetativo_dentro_de_objetivos(self):
        config = cargar_config(None)
        r = simular(config, dias=3, inicio=INICIO)
        o = config.objetivos
        self.assertGreater(r.pct_hr_en_rango, 95)
        self.assertLess(r.temp_max, o.temp_max + 1)
        self.assertGreater(r.suelo_min, config.riego.humedad_suelo_min - 5)
        self.assertGreater(r.riegos, 3)
        self.assertAlmostEqual(r.ec_final, o.ec, delta=config.nutrientes.tolerancia_ec)
        ph_min, ph_max = config.rango_ph
        self.assertTrue(ph_min <= r.ph_final <= ph_max)
        self.assertEqual(r.alarmas, set())

    def test_todas_las_etapas_sin_alarmas(self):
        for etapa in ("plantula", "vegetativo", "floracion", "floracion_tardia", "lavado"):
            with self.subTest(etapa=etapa):
                r = simular(config_desde_dict({"etapa": etapa}), dias=2, inicio=INICIO)
                self.assertEqual(r.alarmas, set())

    def test_floracion_enciende_12_horas(self):
        config = config_desde_dict({"etapa": "floracion", "luz": {"hora_encendido": "20:00"}})
        sim = crear_simulador(config, INICIO)
        controlador = ControladorCultivo(config, sim)
        horas_con_luz = 0
        for _ in range(24 * 60):
            inicio = sim.ahora()
            controlador.ciclo()
            horas_con_luz += sim.estados()["luz"]
            controlador.esperar_siguiente_ciclo(inicio)
        self.assertAlmostEqual(horas_con_luz / 60, 12, delta=0.2)


class TestSeguridad(unittest.TestCase):
    def setUp(self):
        self.config = cargar_config(None)
        self.sim = crear_simulador(self.config, INICIO + timedelta(hours=10))
        self.controlador = ControladorCultivo(self.config, self.sim)

    def test_deposito_vacio_bloquea_riego_y_dosis(self):
        self.sim.volumen_l = 2
        self.sim.humedad_suelo = 10
        self.sim.ec = 0.2
        for _ in range(60):
            self.controlador.ciclo()
            self.sim.dormir(60)
        c = self.controlador.contadores
        self.assertEqual(c["bomba_riego"] + c["dosificador_a"] + c["ph_down"] + c["ph_up"], 0)
        self.assertTrue(any("Nivel bajo" in a for a in self.controlador.alarmas.activas))

    def test_lampara_fundida_genera_alarma(self):
        self.sim.lampara_fundida = True
        for _ in range(15):
            self.controlador.ciclo()
            self.sim.dormir(60)
        self.assertTrue(any("no se detecta luz" in a for a in self.controlador.alarmas.activas))

    def test_sensores_averiados_dejan_estado_seguro(self):
        self.sim.sensores_averiados = {"temperatura", "humedad_relativa", "humedad_suelo", "ec", "ph"}
        self.sim.humedad_suelo = 10
        self.sim.ec = 0.2
        self.controlador.ciclo()
        estados = self.sim.estados()
        self.assertFalse(estados["humidificador"] or estados["deshumidificador"])
        self.assertTrue(estados["extractor"])
        self.assertEqual(sum(self.controlador.contadores.values()), 0)
        self.assertEqual(len(self.controlador.alarmas.activas), 4)

    def test_error_en_el_ciclo_apaga_todo(self):
        self.controlador.ciclo()
        self.assertTrue(self.sim.estados()["luz"])

        def leer_roto():
            raise OSError("bus I2C caído")

        self.sim.leer = leer_roto
        with self.assertLogs("cultivo.controlador", level="ERROR"), self.assertRaises(OSError):
            self.controlador.ejecutar()
        self.assertEqual(self.sim.estados(), dict.fromkeys(ACTUADORES, False))

    def test_pulso_se_apaga_aunque_falle_la_espera(self):
        sim = Simulador(INICIO)

        def dormir_roto(segundos):
            raise KeyboardInterrupt

        sim.dormir = dormir_roto
        with self.assertRaises(KeyboardInterrupt):
            sim.pulso("bomba_riego", 20)
        self.assertFalse(sim.estados()["bomba_riego"])


class TestRegistro(unittest.TestCase):
    def test_csv_con_cabecera_y_eventos(self):
        with tempfile.TemporaryDirectory() as carpeta:
            ruta = Path(carpeta) / "registro.csv"
            config = cargar_config(None)
            registro = RegistroCSV(ruta)
            simular(config, dias=1, inicio=INICIO, registro=registro)
            registro.cerrar()
            with open(ruta, encoding="utf-8") as archivo:
                filas = list(csv.DictReader(archivo))
            self.assertEqual(len(filas), 24 * 60)
            self.assertTrue(any(f["eventos"].startswith("Riego") for f in filas))
            self.assertTrue(any("Luz encendida" in f["eventos"] for f in filas))
            # Reabrir el archivo añade filas sin repetir la cabecera.
            RegistroCSV(ruta).cerrar()
            with open(ruta, encoding="utf-8") as archivo:
                self.assertEqual(archivo.read().count("fecha,etapa"), 1)


class TestCalibracion(unittest.TestCase):
    def setUp(self):
        self.cal = ConfigCalibracion(
            suelo_v_seco=2.8, suelo_v_mojado=1.3, ph_v_7=1.5, ph_v_4=2.03, ec_v_0=0.1, ec_v_1413=1.1
        )

    def test_ph_pasa_por_los_puntos_de_calibracion(self):
        self.assertAlmostEqual(calibracion.ph(1.5, self.cal), 7.0)
        self.assertAlmostEqual(calibracion.ph(2.03, self.cal), 4.0)

    def test_ec_con_compensacion_de_temperatura(self):
        self.assertAlmostEqual(calibracion.ec(1.1, self.cal, 25.0), 1.413)
        self.assertAlmostEqual(calibracion.ec(0.1, self.cal, None), 0.0)
        # A más temperatura la sonda lee más; la compensación lo corrige a la baja.
        self.assertLess(calibracion.ec(1.1, self.cal, 30.0), 1.413)

    def test_humedad_suelo_acotada(self):
        self.assertEqual(calibracion.humedad_suelo(3.0, self.cal), 0)
        self.assertEqual(calibracion.humedad_suelo(1.0, self.cal), 100)
        self.assertAlmostEqual(calibracion.humedad_suelo(2.05, self.cal), 50)

    def test_vpd(self):
        self.assertAlmostEqual(calcular_vpd(25, 60), 1.27, places=2)
        self.assertIsNone(calcular_vpd(None, 60))


if __name__ == "__main__":
    unittest.main()
