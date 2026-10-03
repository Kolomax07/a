import unittest
from pathlib import Path

from cultivo.config import cargar_config, config_desde_dict, parsear_hora
from cultivo.etapas import ETAPAS

RAIZ = Path(__file__).resolve().parent.parent


class TestConfig(unittest.TestCase):
    def test_valores_por_defecto(self):
        config = cargar_config(None)
        self.assertEqual(config.etapa, "vegetativo")
        self.assertEqual(config.objetivos, ETAPAS["vegetativo"])
        self.assertEqual(config.rango_ph, (5.7, 6.3))

    def test_archivo_de_ejemplo_es_valido(self):
        config = cargar_config(RAIZ / "config.ejemplo.toml")
        self.assertIn(config.etapa, config.etapas)

    def test_sobrescribir_etapa_y_ph(self):
        config = config_desde_dict(
            {
                "etapa": "floracion",
                "medio": "tierra",
                "etapas": {"floracion": {"hr_max": 48, "ec": 1.8}},
                "nutrientes": {"ph_max": 6.6},
            }
        )
        self.assertEqual(config.objetivos.hr_max, 48)
        self.assertEqual(config.objetivos.ec, 1.8)
        self.assertEqual(config.objetivos.horas_luz, 12)  # el resto se mantiene
        self.assertEqual(config.rango_ph, (6.2, 6.6))

    def test_errores_claros(self):
        casos = [
            ({"etapa": "cosecha"}, "etapa 'cosecha' no existe"),
            ({"medio": "aire"}, "medio 'aire' no existe"),
            ({"riego": {"humedad_minima": 30}}, "Claves desconocidas en [riego]: humedad_minima"),
            ({"etapas": {"vegetativo": {"hr_min": 60, "hr_max": 62}}}, "hr_max - hr_min"),
            ({"luz": {"hora_encendido": "25:00"}}, "Hora no válida"),
            ({"etapas": {"otoño": {}}}, "Etapa desconocida"),
            ({"riego": {"duracion_s": 900}}, "duracion_s"),
        ]
        for datos, mensaje in casos:
            with self.subTest(datos=datos):
                with self.assertRaises(ValueError) as error:
                    config_desde_dict(datos)
                self.assertIn(mensaje, str(error.exception))

    def test_parsear_hora(self):
        self.assertEqual(parsear_hora("06:30"), 390)
        self.assertEqual(parsear_hora("0:00"), 0)
        for malo in ("6", "aa:bb", "24:00", "12:60"):
            with self.assertRaises(ValueError):
                parsear_hora(malo)


if __name__ == "__main__":
    unittest.main()
