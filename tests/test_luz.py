import unittest

from cultivo.config import ConfigLuz
from cultivo.controladores import ControladorLuz

from .utilidades import lectura


class TestFotoperiodo(unittest.TestCase):
    def test_18_6(self):
        luz = ControladorLuz(ConfigLuz(hora_encendido="06:00"), horas_luz=18)
        self.assertFalse(luz.actualizar(lectura("05:59")))
        self.assertTrue(luz.actualizar(lectura("06:00")))
        self.assertTrue(luz.actualizar(lectura("23:59")))
        self.assertFalse(luz.actualizar(lectura("00:00")))

    def test_12_12_cruzando_medianoche(self):
        luz = ControladorLuz(ConfigLuz(hora_encendido="20:00"), horas_luz=12)
        self.assertFalse(luz.actualizar(lectura("19:59")))
        self.assertTrue(luz.actualizar(lectura("20:00")))
        self.assertTrue(luz.actualizar(lectura("03:00")))
        self.assertTrue(luz.actualizar(lectura("07:59")))
        self.assertFalse(luz.actualizar(lectura("08:00")))

    def test_24_horas(self):
        luz = ControladorLuz(ConfigLuz(), horas_luz=24)
        for hora in ("00:00", "05:59", "06:00", "23:59"):
            self.assertTrue(luz.actualizar(lectura(hora)))


class TestVigilanciaSensorLuz(unittest.TestCase):
    def setUp(self):
        self.luz = ControladorLuz(ConfigLuz(hora_encendido="06:00", minutos_tolerancia=10), horas_luz=12)

    def test_lampara_fundida_avisa_tras_la_tolerancia(self):
        self.luz.actualizar(lectura("07:00", lux=0))
        self.assertEqual(self.luz.alarmas, [])
        self.luz.actualizar(lectura("07:09", lux=0))
        self.assertEqual(self.luz.alarmas, [])
        self.luz.actualizar(lectura("07:10", lux=0))
        self.assertIn("no se detecta luz", self.luz.alarmas[0])

    def test_fuga_de_luz_en_oscuridad(self):
        self.luz.actualizar(lectura("20:00", lux=5000))
        self.luz.actualizar(lectura("20:15", lux=5000))
        self.assertIn("fugas de luz", self.luz.alarmas[0])

    def test_sin_alarma_si_todo_cuadra_o_no_hay_sensor(self):
        for hora, lux in (("07:00", 30000), ("07:30", 30000), ("20:00", 0), ("20:30", 0)):
            self.luz.actualizar(lectura(hora, lux=lux))
            self.assertEqual(self.luz.alarmas, [])
        self.luz.actualizar(lectura("21:00"))
        self.assertEqual(self.luz.alarmas, [])

    def test_la_alarma_se_resuelve_al_volver_la_luz(self):
        self.luz.actualizar(lectura("07:00", lux=0))
        self.luz.actualizar(lectura("07:20", lux=0))
        self.assertTrue(self.luz.alarmas)
        self.luz.actualizar(lectura("07:21", lux=30000))
        self.assertEqual(self.luz.alarmas, [])


if __name__ == "__main__":
    unittest.main()
