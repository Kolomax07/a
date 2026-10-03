import unittest

from cultivo.config import ConfigRiego
from cultivo.controladores import ControladorRiego

from .utilidades import lectura


def controlador(**cambios) -> ControladorRiego:
    valores = dict(humedad_suelo_min=35, duracion_s=20, espera_absorcion_min=45, max_riegos_dia=3)
    valores.update(cambios)
    return ControladorRiego(ConfigRiego(**valores))


class TestRiego(unittest.TestCase):
    def test_riega_cuando_el_sustrato_esta_seco(self):
        r = controlador()
        self.assertEqual(r.actualizar(lectura("10:00", humedad_suelo=40, deposito_ok=True), True), 0)
        self.assertEqual(r.actualizar(lectura("10:01", humedad_suelo=34, deposito_ok=True), True), 20)

    def test_espera_de_absorcion(self):
        r = controlador()
        self.assertEqual(r.actualizar(lectura("10:00", humedad_suelo=30), True), 20)
        self.assertEqual(r.actualizar(lectura("10:44", humedad_suelo=30), True), 0)
        self.assertEqual(r.actualizar(lectura("10:45", humedad_suelo=30), True), 20)

    def test_limite_diario_y_reinicio_a_medianoche(self):
        r = controlador(espera_absorcion_min=0)
        for minuto in range(3):
            self.assertEqual(r.actualizar(lectura(f"10:0{minuto}", humedad_suelo=20), True), 20)
        self.assertEqual(r.actualizar(lectura("10:05", humedad_suelo=20), True), 0)
        self.assertIn("Límite diario de riegos", r.alarmas[0])
        self.assertEqual(r.actualizar(lectura("10:00", dia=1, humedad_suelo=20), True), 20)
        self.assertEqual(r.alarmas, [])

    def test_no_riega_con_deposito_vacio(self):
        r = controlador()
        self.assertEqual(r.actualizar(lectura("10:00", humedad_suelo=20, deposito_ok=False), True), 0)
        self.assertEqual(r.riegos_hoy, 0)

    def test_solo_con_luz(self):
        r = controlador()
        self.assertEqual(r.actualizar(lectura("02:00", humedad_suelo=20), False), 0)
        self.assertEqual(controlador(solo_con_luz=False).actualizar(lectura("02:00", humedad_suelo=20), False), 20)

    def test_sensor_averiado_no_riega(self):
        r = controlador()
        self.assertEqual(r.actualizar(lectura("10:00"), True), 0)
        self.assertIn("Sin lectura de humedad del sustrato", r.alarmas[0])


if __name__ == "__main__":
    unittest.main()
