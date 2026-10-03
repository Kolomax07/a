import unittest

from cultivo.config import ConfigHumedad
from cultivo.controladores import ControladorHumedad
from cultivo.etapas import ObjetivosEtapa

from .utilidades import lectura

OBJETIVOS = ObjetivosEtapa(horas_luz=18, hr_min=50, hr_max=65, temp_max=28, ec=1.2)


def controlador(tiempo_minimo_s: float = 0) -> ControladorHumedad:
    return ControladorHumedad(ConfigHumedad(histeresis=3, tiempo_minimo_ciclo_s=tiempo_minimo_s), OBJETIVOS)


class TestHumedad(unittest.TestCase):
    def test_humidificador_con_histeresis(self):
        c = controlador()
        self.assertTrue(c.actualizar(lectura("12:00", humedad_relativa=49, temperatura=24))["humidificador"])
        # Sigue encendido dentro de la banda muerta (50–53)...
        self.assertTrue(c.actualizar(lectura("12:01", humedad_relativa=52, temperatura=24))["humidificador"])
        # ...y se apaga al llegar a hr_min + histéresis.
        self.assertFalse(c.actualizar(lectura("12:02", humedad_relativa=53, temperatura=24))["humidificador"])

    def test_deshumidificador_y_extractor_con_humedad_alta(self):
        c = controlador()
        estado = c.actualizar(lectura("12:00", humedad_relativa=70, temperatura=24))
        self.assertEqual(estado, {"humidificador": False, "deshumidificador": True, "extractor": True})
        estado = c.actualizar(lectura("12:01", humedad_relativa=63, temperatura=24))
        self.assertTrue(estado["deshumidificador"])
        estado = c.actualizar(lectura("12:02", humedad_relativa=62, temperatura=24))
        self.assertEqual(estado, {"humidificador": False, "deshumidificador": False, "extractor": False})

    def test_en_rango_todo_apagado(self):
        estado = controlador().actualizar(lectura("12:00", humedad_relativa=57, temperatura=24))
        self.assertEqual(estado, {"humidificador": False, "deshumidificador": False, "extractor": False})

    def test_extractor_por_temperatura_con_histeresis(self):
        c = controlador()
        self.assertTrue(c.actualizar(lectura("12:00", humedad_relativa=57, temperatura=28.5))["extractor"])
        self.assertTrue(c.actualizar(lectura("12:01", humedad_relativa=57, temperatura=27.5))["extractor"])
        self.assertFalse(c.actualizar(lectura("12:02", humedad_relativa=57, temperatura=27.0))["extractor"])

    def test_tiempo_minimo_entre_cambios(self):
        c = controlador(tiempo_minimo_s=120)
        self.assertTrue(c.actualizar(lectura("12:00", humedad_relativa=45, temperatura=24))["humidificador"])
        self.assertTrue(c.actualizar(lectura("12:01", humedad_relativa=60, temperatura=24))["humidificador"])
        self.assertFalse(c.actualizar(lectura("12:02", humedad_relativa=60, temperatura=24))["humidificador"])

    def test_nunca_humidificador_y_deshumidificador_a_la_vez(self):
        c = controlador(tiempo_minimo_s=600)
        for minuto, hr in enumerate([45, 70, 45, 70, 45, 70]):
            estado = c.actualizar(lectura(f"12:{minuto:02d}", humedad_relativa=hr, temperatura=24))
            self.assertFalse(estado["humidificador"] and estado["deshumidificador"])

    def test_sensor_averiado_apaga_humedad_y_deja_extractor(self):
        c = controlador(tiempo_minimo_s=600)
        c.actualizar(lectura("12:00", humedad_relativa=45, temperatura=24))
        estado = c.actualizar(lectura("12:01"))
        self.assertEqual(estado, {"humidificador": False, "deshumidificador": False, "extractor": True})
        self.assertEqual(len(c.alarmas), 2)

    def test_temperatura_critica(self):
        c = controlador()
        c.actualizar(lectura("12:00", humedad_relativa=57, temperatura=31))
        self.assertTrue(any("Temperatura crítica" in a for a in c.alarmas))


if __name__ == "__main__":
    unittest.main()
