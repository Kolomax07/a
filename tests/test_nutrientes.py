import unittest

from cultivo.config import ConfigNutrientes
from cultivo.controladores import ControladorNutrientes

from .utilidades import lectura

RANGO_PH = (5.7, 6.3)


def controlador(ec_objetivo: float = 1.2, **cambios) -> ControladorNutrientes:
    valores = dict(
        tolerancia_ec=0.15,
        ml_por_dosis=2.0,
        ml_por_dosis_ph=0.5,
        caudal_ml_s=1.0,
        espera_mezcla_min=15,
        max_dosis_nutrientes_dia=3,
        max_dosis_ph_dia=3,
    )
    valores.update(cambios)
    return ControladorNutrientes(ConfigNutrientes(**valores), ec_objetivo, RANGO_PH)


DOSIS_AB = [("dosificador_a", 2.0), ("dosificador_b", 2.0)]


class TestNutrientes(unittest.TestCase):
    def test_todo_en_rango_no_dosifica(self):
        self.assertEqual(controlador().actualizar(lectura("10:00", ec=1.2, ph=6.0)), [])

    def test_ec_baja_dosifica_a_y_b_hasta_el_objetivo(self):
        n = controlador()
        self.assertEqual(n.actualizar(lectura("10:00", ec=1.0, ph=6.0)), DOSIS_AB)
        # Ya dentro de la tolerancia pero aún por debajo del objetivo: sigue subiendo.
        self.assertEqual(n.actualizar(lectura("10:15", ec=1.1, ph=6.0)), DOSIS_AB)
        self.assertEqual(n.actualizar(lectura("10:30", ec=1.2, ph=6.0)), [])
        # Una vez alcanzado, no vuelve a dosificar hasta salir de la tolerancia.
        self.assertEqual(n.actualizar(lectura("10:45", ec=1.1, ph=6.0)), [])

    def test_espera_de_mezcla(self):
        n = controlador()
        self.assertEqual(n.actualizar(lectura("10:00", ec=0.8, ph=6.0)), DOSIS_AB)
        self.assertEqual(n.actualizar(lectura("10:14", ec=0.8, ph=6.0)), [])
        self.assertEqual(n.actualizar(lectura("10:15", ec=0.8, ph=6.0)), DOSIS_AB)

    def test_primero_ec_y_despues_ph(self):
        n = controlador()
        self.assertEqual(n.actualizar(lectura("10:00", ec=1.0, ph=6.8)), DOSIS_AB)
        self.assertEqual(n.actualizar(lectura("10:15", ec=1.25, ph=6.7)), [("ph_down", 0.5)])

    def test_ph_se_corrige_hasta_el_centro_del_rango(self):
        n = controlador()
        self.assertEqual(n.actualizar(lectura("10:00", ec=1.2, ph=6.4)), [("ph_down", 0.5)])
        self.assertEqual(n.actualizar(lectura("10:15", ec=1.2, ph=6.1)), [("ph_down", 0.5)])
        self.assertEqual(n.actualizar(lectura("10:30", ec=1.2, ph=6.0)), [])

    def test_ph_bajo_sube(self):
        self.assertEqual(controlador().actualizar(lectura("10:00", ec=1.2, ph=5.5)), [("ph_up", 0.5)])

    def test_limite_diario_de_nutrientes_no_bloquea_el_ph(self):
        n = controlador(espera_mezcla_min=0)
        for minuto in range(3):
            self.assertEqual(n.actualizar(lectura(f"10:0{minuto}", ec=0.5, ph=6.0)), DOSIS_AB)
        self.assertEqual(n.actualizar(lectura("10:05", ec=0.5, ph=6.0)), [])
        self.assertIn("Límite diario de dosis de nutrientes", n.alarmas[0])
        self.assertEqual(n.actualizar(lectura("10:06", ec=0.5, ph=6.5)), [("ph_down", 0.5)])
        self.assertEqual(n.actualizar(lectura("10:00", dia=1, ec=0.5, ph=6.0)), DOSIS_AB)

    def test_limite_diario_de_ph(self):
        n = controlador(espera_mezcla_min=0)
        for minuto in range(3):
            n.actualizar(lectura(f"10:0{minuto}", ec=1.2, ph=7.0))
        self.assertEqual(n.actualizar(lectura("10:05", ec=1.2, ph=7.0)), [])
        self.assertIn("Límite diario de correcciones de pH", n.alarmas[0])

    def test_sondas_averiadas_o_fuera_del_agua(self):
        for ec, ph in ((None, 6.0), (9.0, 6.0), (1.2, None), (1.2, 0.5)):
            n = controlador()
            self.assertEqual(n.actualizar(lectura("10:00", ec=ec, ph=ph)), [])
            self.assertIn("no válida", n.alarmas[0])

    def test_deposito_bajo_no_dosifica(self):
        self.assertEqual(controlador().actualizar(lectura("10:00", ec=0.5, ph=7.0, deposito_ok=False)), [])

    def test_ec_alta_avisa(self):
        n = controlador()
        self.assertEqual(n.actualizar(lectura("10:00", ec=1.8, ph=6.0)), [])
        self.assertIn("EC alta", n.alarmas[0])

    def test_lavado_solo_corrige_ph(self):
        n = controlador(ec_objetivo=0)
        self.assertEqual(n.actualizar(lectura("10:00", ec=0.3, ph=6.0)), [])
        self.assertEqual(n.alarmas, [])
        self.assertEqual(n.actualizar(lectura("10:15", ec=0.3, ph=6.6)), [("ph_down", 0.5)])


if __name__ == "__main__":
    unittest.main()
