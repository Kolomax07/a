// Tests de la lógica de control en el PC:  pio test -e native
#include <cultivo.h>
#include <unity.h>

#include <cstdio>
#include <cstring>

using namespace cultivo;

namespace {

constexpr ObjetivosEtapa VEGETATIVO = {18, 50, 65, 28, 1.2f};
constexpr uint64_t MINUTO = 60 * 1000ULL;
constexpr uint64_t DIA = 24 * 60 * MINUTO;

// Lecturas a la hora "hh:mm" del día `dia` (el reloj monotónico arranca en el día 0 a las 00:00).
Lecturas lectura(int hh, int mm, int dia = 0) {
  Lecturas l;
  l.minutoDelDia = static_cast<int16_t>(hh * 60 + mm);
  l.dia = dia;
  l.ms = dia * DIA + l.minutoDelDia * MINUTO;
  return l;
}

Lecturas clima(int hh, int mm, float hr, float temperatura = 24) {
  Lecturas l = lectura(hh, mm);
  l.humedadRelativa = hr;
  l.temperatura = temperatura;
  return l;
}

Lecturas suelo(int hh, int mm, float humedad, int dia = 0) {
  Lecturas l = lectura(hh, mm, dia);
  l.humedadSuelo = humedad;
  return l;
}

Lecturas deposito(int hh, int mm, float ec, float ph, int dia = 0) {
  Lecturas l = lectura(hh, mm, dia);
  l.ec = ec;
  l.ph = ph;
  return l;
}

ConfigNutrientes configNutrientes() {
  ConfigNutrientes c;
  c.maxDosisNutrientesDia = 3;
  c.maxDosisPhDia = 3;
  return c;
}

void esperarDosisAB(const Dosis& d) {
  TEST_ASSERT_EQUAL(2, d.cantidad);
  TEST_ASSERT_TRUE(d.actuadores[0] == Actuador::DosificadorA);
  TEST_ASSERT_TRUE(d.actuadores[1] == Actuador::DosificadorB);
  TEST_ASSERT_EQUAL_UINT32(2000, d.ms);
}

void esperarPh(const Dosis& d, Actuador a) {
  TEST_ASSERT_EQUAL(1, d.cantidad);
  TEST_ASSERT_TRUE(d.actuadores[0] == a);
  TEST_ASSERT_EQUAL_UINT32(500, d.ms);
}

}  // namespace

void setUp() {}
void tearDown() {}

// --- Luz ---------------------------------------------------------------------

void test_luz_18_6() {
  ControladorLuz luz(ConfigLuz{}, 18);
  TEST_ASSERT_FALSE(luz.actualizar(lectura(5, 59)));
  TEST_ASSERT_TRUE(luz.actualizar(lectura(6, 0)));
  TEST_ASSERT_TRUE(luz.actualizar(lectura(23, 59)));
  TEST_ASSERT_FALSE(luz.actualizar(lectura(0, 0)));
}

void test_luz_12_12_cruzando_medianoche() {
  ConfigLuz c;
  c.minutoEncendido = 20 * 60;
  ControladorLuz luz(c, 12);
  TEST_ASSERT_FALSE(luz.actualizar(lectura(19, 59)));
  TEST_ASSERT_TRUE(luz.actualizar(lectura(20, 0)));
  TEST_ASSERT_TRUE(luz.actualizar(lectura(3, 0)));
  TEST_ASSERT_TRUE(luz.actualizar(lectura(7, 59)));
  TEST_ASSERT_FALSE(luz.actualizar(lectura(8, 0)));
}

void test_luz_24_horas() {
  ControladorLuz luz(ConfigLuz{}, 24);
  TEST_ASSERT_TRUE(luz.actualizar(lectura(0, 0)));
  TEST_ASSERT_TRUE(luz.actualizar(lectura(5, 59)));
  TEST_ASSERT_TRUE(luz.actualizar(lectura(23, 59)));
}

void test_luz_sin_hora_apagada() {
  ControladorLuz luz(ConfigLuz{}, 18);
  Lecturas l = lectura(12, 0);
  l.horaValida = false;
  TEST_ASSERT_FALSE(luz.actualizar(l));
}

void test_lampara_fundida_tras_la_tolerancia() {
  ControladorLuz luz(ConfigLuz{}, 12);
  Lecturas l = lectura(7, 0);
  l.lux = 0;
  luz.actualizar(l);
  TEST_ASSERT_EQUAL_UINT32(0, luz.alarmas());
  l = lectura(7, 9);
  l.lux = 0;
  luz.actualizar(l);
  TEST_ASSERT_EQUAL_UINT32(0, luz.alarmas());
  l = lectura(7, 10);
  l.lux = 0;
  luz.actualizar(l);
  TEST_ASSERT_EQUAL_UINT32(alarma::LAMPARA_NO_ENCIENDE, luz.alarmas());
  l = lectura(7, 11);
  l.lux = 30000;
  luz.actualizar(l);
  TEST_ASSERT_EQUAL_UINT32(0, luz.alarmas());
}

void test_fuga_de_luz() {
  ControladorLuz luz(ConfigLuz{}, 12);
  Lecturas l = lectura(20, 0);
  l.lux = 5000;
  luz.actualizar(l);
  l = lectura(20, 15);
  l.lux = 5000;
  luz.actualizar(l);
  TEST_ASSERT_EQUAL_UINT32(alarma::FUGA_DE_LUZ, luz.alarmas());
}

// --- Humedad -----------------------------------------------------------------

void test_humidificador_con_histeresis() {
  ConfigHumedad c;
  c.tiempoMinimoCicloMs = 0;
  ControladorHumedad h(c, VEGETATIVO);
  TEST_ASSERT_TRUE(h.actualizar(clima(12, 0, 49)).humidificador);
  TEST_ASSERT_TRUE(h.actualizar(clima(12, 1, 52)).humidificador);  // banda muerta 50–53
  TEST_ASSERT_FALSE(h.actualizar(clima(12, 2, 53)).humidificador);
}

void test_deshumidificador_y_extractor() {
  ConfigHumedad c;
  c.tiempoMinimoCicloMs = 0;
  ControladorHumedad h(c, VEGETATIVO);
  EstadoClima e = h.actualizar(clima(12, 0, 70));
  TEST_ASSERT_FALSE(e.humidificador);
  TEST_ASSERT_TRUE(e.deshumidificador);
  TEST_ASSERT_TRUE(e.extractor);
  TEST_ASSERT_TRUE(h.actualizar(clima(12, 1, 63)).deshumidificador);
  e = h.actualizar(clima(12, 2, 62));
  TEST_ASSERT_FALSE(e.deshumidificador);
  TEST_ASSERT_FALSE(e.extractor);
}

void test_extractor_por_temperatura() {
  ConfigHumedad c;
  c.tiempoMinimoCicloMs = 0;
  ControladorHumedad h(c, VEGETATIVO);
  TEST_ASSERT_TRUE(h.actualizar(clima(12, 0, 57, 28.5f)).extractor);
  TEST_ASSERT_TRUE(h.actualizar(clima(12, 1, 57, 27.5f)).extractor);
  TEST_ASSERT_FALSE(h.actualizar(clima(12, 2, 57, 27.0f)).extractor);
  h.actualizar(clima(12, 3, 57, 31));
  TEST_ASSERT_TRUE(h.alarmas() & alarma::TEMPERATURA_CRITICA);
}

void test_tiempo_minimo_entre_cambios() {
  ControladorHumedad h(ConfigHumedad{}, VEGETATIVO);  // 120 s
  TEST_ASSERT_TRUE(h.actualizar(clima(12, 0, 45)).humidificador);
  TEST_ASSERT_TRUE(h.actualizar(clima(12, 1, 60)).humidificador);
  TEST_ASSERT_FALSE(h.actualizar(clima(12, 2, 60)).humidificador);
}

void test_nunca_humidificador_y_deshumidificador_a_la_vez() {
  ConfigHumedad c;
  c.tiempoMinimoCicloMs = 600 * 1000;
  ControladorHumedad h(c, VEGETATIVO);
  const float hrs[] = {45, 70, 45, 70, 45, 70};
  for (int i = 0; i < 6; i++) {
    EstadoClima e = h.actualizar(clima(12, i, hrs[i]));
    TEST_ASSERT_FALSE(e.humidificador && e.deshumidificador);
  }
}

void test_sensor_clima_averiado_estado_seguro() {
  ConfigHumedad c;
  c.tiempoMinimoCicloMs = 600 * 1000;
  ControladorHumedad h(c, VEGETATIVO);
  h.actualizar(clima(12, 0, 45));
  EstadoClima e = h.actualizar(lectura(12, 1));
  TEST_ASSERT_FALSE(e.humidificador);
  TEST_ASSERT_FALSE(e.deshumidificador);
  TEST_ASSERT_TRUE(e.extractor);
  TEST_ASSERT_EQUAL_UINT32(alarma::SIN_HUMEDAD_RELATIVA | alarma::SIN_TEMPERATURA, h.alarmas());
}

// --- Riego -------------------------------------------------------------------

ConfigRiego configRiego() {
  ConfigRiego c;
  c.maxRiegosDia = 3;
  return c;
}

void test_riega_con_sustrato_seco() {
  ControladorRiego r(configRiego());
  TEST_ASSERT_EQUAL_UINT32(0, r.actualizar(suelo(10, 0, 40), true));
  TEST_ASSERT_EQUAL_UINT32(20000, r.actualizar(suelo(10, 1, 34), true));
}

void test_espera_de_absorcion() {
  ControladorRiego r(configRiego());
  TEST_ASSERT_EQUAL_UINT32(20000, r.actualizar(suelo(10, 0, 30), true));
  TEST_ASSERT_EQUAL_UINT32(0, r.actualizar(suelo(10, 44, 30), true));
  TEST_ASSERT_EQUAL_UINT32(20000, r.actualizar(suelo(10, 45, 30), true));
}

void test_limite_diario_de_riegos_y_reinicio() {
  ConfigRiego c = configRiego();
  c.esperaAbsorcionMs = 0;
  ControladorRiego r(c);
  for (int m = 0; m < 3; m++) TEST_ASSERT_EQUAL_UINT32(20000, r.actualizar(suelo(10, m, 20), true));
  TEST_ASSERT_EQUAL_UINT32(0, r.actualizar(suelo(10, 5, 20), true));
  TEST_ASSERT_EQUAL_UINT32(alarma::LIMITE_RIEGOS, r.alarmas());
  TEST_ASSERT_EQUAL_UINT32(20000, r.actualizar(suelo(10, 0, 20, 1), true));
  TEST_ASSERT_EQUAL_UINT32(0, r.alarmas());
}

void test_no_riega_con_deposito_bajo_ni_de_noche() {
  ControladorRiego r(configRiego());
  Lecturas l = suelo(10, 0, 20);
  l.deposito = Nivel::Bajo;
  TEST_ASSERT_EQUAL_UINT32(0, r.actualizar(l, true));
  TEST_ASSERT_EQUAL_UINT32(0, r.actualizar(suelo(2, 0, 20), false));
  ConfigRiego c = configRiego();
  c.soloConLuz = false;
  ControladorRiego deNoche(c);
  TEST_ASSERT_EQUAL_UINT32(20000, deNoche.actualizar(suelo(2, 0, 20), false));
}

void test_sensor_sustrato_averiado() {
  ControladorRiego r(configRiego());
  TEST_ASSERT_EQUAL_UINT32(0, r.actualizar(lectura(10, 0), true));
  TEST_ASSERT_EQUAL_UINT32(alarma::SIN_HUMEDAD_SUSTRATO, r.alarmas());
}

// --- Nutrientes --------------------------------------------------------------

void test_todo_en_rango_no_dosifica() {
  ControladorNutrientes n(configNutrientes(), 1.2f, {5.7f, 6.3f});
  TEST_ASSERT_EQUAL(0, n.actualizar(deposito(10, 0, 1.2f, 6.0f)).cantidad);
}

void test_ec_baja_dosifica_a_y_b_hasta_el_objetivo() {
  ControladorNutrientes n(configNutrientes(), 1.2f, {5.7f, 6.3f});
  esperarDosisAB(n.actualizar(deposito(10, 0, 1.0f, 6.0f)));
  esperarDosisAB(n.actualizar(deposito(10, 15, 1.1f, 6.0f)));  // dentro de tolerancia pero bajo el objetivo
  TEST_ASSERT_EQUAL(0, n.actualizar(deposito(10, 30, 1.2f, 6.0f)).cantidad);
  TEST_ASSERT_EQUAL(0, n.actualizar(deposito(10, 45, 1.1f, 6.0f)).cantidad);
}

void test_espera_de_mezcla() {
  ControladorNutrientes n(configNutrientes(), 1.2f, {5.7f, 6.3f});
  esperarDosisAB(n.actualizar(deposito(10, 0, 0.8f, 6.0f)));
  TEST_ASSERT_EQUAL(0, n.actualizar(deposito(10, 14, 0.8f, 6.0f)).cantidad);
  esperarDosisAB(n.actualizar(deposito(10, 15, 0.8f, 6.0f)));
}

void test_primero_ec_y_despues_ph() {
  ControladorNutrientes n(configNutrientes(), 1.2f, {5.7f, 6.3f});
  esperarDosisAB(n.actualizar(deposito(10, 0, 1.0f, 6.8f)));
  esperarPh(n.actualizar(deposito(10, 15, 1.25f, 6.7f)), Actuador::PhDown);
}

void test_ph_hasta_el_centro_del_rango() {
  ControladorNutrientes n(configNutrientes(), 1.2f, {5.7f, 6.3f});
  esperarPh(n.actualizar(deposito(10, 0, 1.2f, 6.4f)), Actuador::PhDown);
  esperarPh(n.actualizar(deposito(10, 15, 1.2f, 6.1f)), Actuador::PhDown);
  TEST_ASSERT_EQUAL(0, n.actualizar(deposito(10, 30, 1.2f, 6.0f)).cantidad);
  ControladorNutrientes subir(configNutrientes(), 1.2f, {5.7f, 6.3f});
  esperarPh(subir.actualizar(deposito(10, 0, 1.2f, 5.5f)), Actuador::PhUp);
}

void test_limite_de_nutrientes_no_bloquea_el_ph() {
  ConfigNutrientes c = configNutrientes();
  c.esperaMezclaMs = 0;
  ControladorNutrientes n(c, 1.2f, {5.7f, 6.3f});
  for (int m = 0; m < 3; m++) esperarDosisAB(n.actualizar(deposito(10, m, 0.5f, 6.0f)));
  TEST_ASSERT_EQUAL(0, n.actualizar(deposito(10, 5, 0.5f, 6.0f)).cantidad);
  TEST_ASSERT_EQUAL_UINT32(alarma::LIMITE_NUTRIENTES, n.alarmas());
  esperarPh(n.actualizar(deposito(10, 6, 0.5f, 6.5f)), Actuador::PhDown);
  esperarDosisAB(n.actualizar(deposito(10, 0, 0.5f, 6.0f, 1)));
}

void test_limite_de_ph() {
  ConfigNutrientes c = configNutrientes();
  c.esperaMezclaMs = 0;
  ControladorNutrientes n(c, 1.2f, {5.7f, 6.3f});
  for (int m = 0; m < 3; m++) n.actualizar(deposito(10, m, 1.2f, 7.0f));
  TEST_ASSERT_EQUAL(0, n.actualizar(deposito(10, 5, 1.2f, 7.0f)).cantidad);
  TEST_ASSERT_EQUAL_UINT32(alarma::LIMITE_PH, n.alarmas());
}

void test_sondas_averiadas_o_fuera_del_agua() {
  const float casos[][2] = {{NAN, 6.0f}, {9.0f, 6.0f}, {0.0f, 6.0f}, {1.2f, NAN}, {1.2f, 0.5f}};
  const Alarmas esperadas[] = {alarma::EC_NO_VALIDA, alarma::EC_NO_VALIDA, alarma::EC_NO_VALIDA,
                               alarma::PH_NO_VALIDO, alarma::PH_NO_VALIDO};
  for (int i = 0; i < 5; i++) {
    ControladorNutrientes n(configNutrientes(), 1.2f, {5.7f, 6.3f});
    TEST_ASSERT_EQUAL(0, n.actualizar(deposito(10, 0, casos[i][0], casos[i][1])).cantidad);
    TEST_ASSERT_EQUAL_UINT32(esperadas[i], n.alarmas());
  }
}

void test_deposito_bajo_no_dosifica() {
  ControladorNutrientes n(configNutrientes(), 1.2f, {5.7f, 6.3f});
  Lecturas l = deposito(10, 0, 0.5f, 7.0f);
  l.deposito = Nivel::Bajo;
  TEST_ASSERT_EQUAL(0, n.actualizar(l).cantidad);
}

void test_ec_alta_avisa() {
  ControladorNutrientes n(configNutrientes(), 1.2f, {5.7f, 6.3f});
  TEST_ASSERT_EQUAL(0, n.actualizar(deposito(10, 0, 1.8f, 6.0f)).cantidad);
  TEST_ASSERT_EQUAL_UINT32(alarma::EC_ALTA, n.alarmas());
}

void test_lavado_solo_corrige_ph() {
  ControladorNutrientes n(configNutrientes(), 0.0f, {5.7f, 6.3f});
  TEST_ASSERT_EQUAL(0, n.actualizar(deposito(10, 0, 0.0f, 6.0f)).cantidad);  // agua de ósmosis: válida en lavado
  TEST_ASSERT_EQUAL_UINT32(0, n.alarmas());
  esperarPh(n.actualizar(deposito(10, 15, 0.3f, 6.6f)), Actuador::PhDown);
}

// --- Secuenciador y controlador principal ------------------------------------

struct SalidasDePrueba : Salidas {
  bool estados[NUM_ACTUADORES] = {};
  void fijar(Actuador a, bool encendido) override { estados[static_cast<uint8_t>(a)] = encendido; }
  bool operator[](Actuador a) const { return estados[static_cast<uint8_t>(a)]; }
};

void test_secuenciador_a_pausa_b() {
  SalidasDePrueba s;
  Secuenciador sec;
  sec.encolar(Actuador::DosificadorA, 2000, 30000);
  sec.encolar(Actuador::DosificadorB, 2000, 0);
  sec.atender(0, s);
  TEST_ASSERT_TRUE(s[Actuador::DosificadorA]);
  sec.atender(1999, s);
  TEST_ASSERT_TRUE(s[Actuador::DosificadorA]);
  sec.atender(2000, s);
  TEST_ASSERT_FALSE(s[Actuador::DosificadorA]);
  TEST_ASSERT_FALSE(s[Actuador::DosificadorB]);
  TEST_ASSERT_TRUE(sec.ocupado());
  sec.atender(31999, s);
  TEST_ASSERT_FALSE(s[Actuador::DosificadorB]);
  sec.atender(32000, s);
  TEST_ASSERT_TRUE(s[Actuador::DosificadorB]);
  sec.atender(34000, s);
  TEST_ASSERT_FALSE(s[Actuador::DosificadorB]);
  TEST_ASSERT_FALSE(sec.ocupado());
}

void test_secuenciador_cancelar_apaga_la_bomba() {
  SalidasDePrueba s;
  Secuenciador sec;
  sec.encolar(Actuador::BombaRiego, 20000, 0);
  sec.atender(0, s);
  TEST_ASSERT_TRUE(s[Actuador::BombaRiego]);
  sec.cancelar(s);
  TEST_ASSERT_FALSE(s[Actuador::BombaRiego]);
  TEST_ASSERT_FALSE(sec.ocupado());
}

ConfigCultivo configCultivo() {
  ConfigCultivo c;
  const ObjetivosEtapa etapas[NUM_ETAPAS] = {
      {18, 65, 75, 27, 0.6f}, {18, 50, 65, 28, 1.2f}, {12, 40, 50, 26, 1.6f},
      {12, 35, 45, 25, 1.4f}, {12, 35, 45, 25, 0.0f},
  };
  for (uint8_t i = 0; i < NUM_ETAPAS; i++) c.etapas[i] = etapas[i];
  return c;
}

Lecturas todoEnOrden(int hh, int mm) {
  Lecturas l = lectura(hh, mm);
  l.temperatura = 24;
  l.humedadRelativa = 57;
  l.humedadSuelo = 50;
  l.ec = 1.2f;
  l.ph = 6.0f;
  l.deposito = Nivel::Ok;
  return l;
}

void test_controlador_riega_y_registra_eventos() {
  SalidasDePrueba s;
  ControladorCultivo c(configCultivo(), Etapa::Vegetativo, s);
  Lecturas l = todoEnOrden(10, 0);
  l.humedadSuelo = 30;
  c.ciclo(l);
  TEST_ASSERT_TRUE(s[Actuador::Luz]);
  TEST_ASSERT_TRUE(s[Actuador::BombaRiego]);
  TEST_ASSERT_EQUAL_UINT32(1, c.pulsos(Actuador::BombaRiego));
  c.atender(l.ms + 20000);
  TEST_ASSERT_FALSE(s[Actuador::BombaRiego]);
  TEST_ASSERT_EQUAL(2, c.bitacora().cantidad());
  TEST_ASSERT_EQUAL_STRING("Riego 20 s (sustrato al 30 %)", c.bitacora().entrada(0).texto);
  TEST_ASSERT_EQUAL_STRING("Luz encendida", c.bitacora().entrada(1).texto);
}

void test_controlador_alarmas_al_aparecer_y_resolverse() {
  SalidasDePrueba s;
  ControladorCultivo c(configCultivo(), Etapa::Vegetativo, s);
  Lecturas l = todoEnOrden(10, 0);
  l.deposito = Nivel::Bajo;
  l.humedadSuelo = 10;
  c.ciclo(l);
  TEST_ASSERT_EQUAL_UINT32(alarma::DEPOSITO_BAJO, c.alarmas());
  TEST_ASSERT_FALSE(s[Actuador::BombaRiego]);
  c.ciclo(l);  // la misma alarma no se vuelve a anotar
  l = todoEnOrden(10, 2);
  c.ciclo(l);
  TEST_ASSERT_EQUAL_UINT32(0, c.alarmas());
  TEST_ASSERT_EQUAL_UINT32(alarma::DEPOSITO_BAJO, c.historialAlarmas());
  TEST_ASSERT_EQUAL(3, c.bitacora().cantidad());
  TEST_ASSERT_EQUAL(0, std::strncmp("Resuelta: Nivel bajo", c.bitacora().entrada(0).texto, 20));
  TEST_ASSERT_EQUAL(0, std::strncmp("ALARMA: Nivel bajo", c.bitacora().entrada(1).texto, 18));
}

void test_controlador_cambiar_etapa_a_floracion() {
  SalidasDePrueba s;
  ControladorCultivo c(configCultivo(), Etapa::Vegetativo, s);
  c.ciclo(todoEnOrden(20, 0));
  TEST_ASSERT_TRUE(s[Actuador::Luz]);  // 18/6 desde las 06:00
  c.cambiarEtapa(Etapa::Floracion);
  c.ciclo(todoEnOrden(20, 1));
  TEST_ASSERT_FALSE(s[Actuador::Luz]);  // 12/12: apagada desde las 18:00
  TEST_ASSERT_TRUE(c.etapa() == Etapa::Floracion);
  TEST_ASSERT_EQUAL_FLOAT(1.6f, c.objetivos().ec);
}

void test_controlador_apagar_todo() {
  SalidasDePrueba s;
  ControladorCultivo c(configCultivo(), Etapa::Vegetativo, s);
  Lecturas l = todoEnOrden(10, 0);
  l.humedadSuelo = 30;
  l.humedadRelativa = 40;
  c.ciclo(l);
  c.apagarTodo();
  for (uint8_t i = 0; i < NUM_ACTUADORES; i++) TEST_ASSERT_FALSE(s.estados[i]);
}

void test_controlador_prueba_manual_de_rele() {
  SalidasDePrueba s;
  ControladorCultivo c(configCultivo(), Etapa::Vegetativo, s);
  Lecturas l = todoEnOrden(10, 0);
  c.ciclo(l);
  TEST_ASSERT_FALSE(c.probar(Actuador::Luz, 2000));  // ya encendida: no se toca
  TEST_ASSERT_TRUE(c.probar(Actuador::DosificadorA, 2000));
  TEST_ASSERT_FALSE(c.probar(Actuador::DosificadorB, 2000));  // otra bomba en marcha
  c.atender(l.ms);
  TEST_ASSERT_TRUE(s[Actuador::DosificadorA]);
  c.atender(l.ms + 2000);
  TEST_ASSERT_FALSE(s[Actuador::DosificadorA]);
  TEST_ASSERT_TRUE(c.probar(Actuador::DosificadorB, 2000));
  TEST_ASSERT_EQUAL_UINT32(0, c.pulsos(Actuador::DosificadorA));  // no cuenta como dosis
}

void test_bitacora_circular() {
  Bitacora b;
  char texto[8];
  for (int i = 0; i < 25; i++) {
    std::snprintf(texto, sizeof texto, "e%d", i);
    b.anotar(lectura(0, 0), texto);
  }
  TEST_ASSERT_EQUAL(Bitacora::CAPACIDAD, b.cantidad());
  TEST_ASSERT_EQUAL_STRING("e24", b.entrada(0).texto);
  TEST_ASSERT_EQUAL_STRING("e5", b.entrada(Bitacora::CAPACIDAD - 1).texto);
}

// --- Calibración -------------------------------------------------------------

void test_calibracion_y_vpd() {
  CalibracionSondas cal;
  cal.ecV0 = 0.1f;
  cal.ecV1413 = 1.1f;
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 7.0f, ph(1.50f, cal));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.0f, ph(2.03f, cal));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.413f, ec(1.1f, cal, 25));
  TEST_ASSERT_TRUE(ec(1.1f, cal, 30) < 1.413f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, humedadSuelo(3.0f, cal));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, humedadSuelo(1.0f, cal));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, humedadSuelo(2.05f, cal));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.27f, vpd(25, 60));
  TEST_ASSERT_TRUE(std::isnan(vpd(NAN, 60)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_luz_18_6);
  RUN_TEST(test_luz_12_12_cruzando_medianoche);
  RUN_TEST(test_luz_24_horas);
  RUN_TEST(test_luz_sin_hora_apagada);
  RUN_TEST(test_lampara_fundida_tras_la_tolerancia);
  RUN_TEST(test_fuga_de_luz);
  RUN_TEST(test_humidificador_con_histeresis);
  RUN_TEST(test_deshumidificador_y_extractor);
  RUN_TEST(test_extractor_por_temperatura);
  RUN_TEST(test_tiempo_minimo_entre_cambios);
  RUN_TEST(test_nunca_humidificador_y_deshumidificador_a_la_vez);
  RUN_TEST(test_sensor_clima_averiado_estado_seguro);
  RUN_TEST(test_riega_con_sustrato_seco);
  RUN_TEST(test_espera_de_absorcion);
  RUN_TEST(test_limite_diario_de_riegos_y_reinicio);
  RUN_TEST(test_no_riega_con_deposito_bajo_ni_de_noche);
  RUN_TEST(test_sensor_sustrato_averiado);
  RUN_TEST(test_todo_en_rango_no_dosifica);
  RUN_TEST(test_ec_baja_dosifica_a_y_b_hasta_el_objetivo);
  RUN_TEST(test_espera_de_mezcla);
  RUN_TEST(test_primero_ec_y_despues_ph);
  RUN_TEST(test_ph_hasta_el_centro_del_rango);
  RUN_TEST(test_limite_de_nutrientes_no_bloquea_el_ph);
  RUN_TEST(test_limite_de_ph);
  RUN_TEST(test_sondas_averiadas_o_fuera_del_agua);
  RUN_TEST(test_deposito_bajo_no_dosifica);
  RUN_TEST(test_ec_alta_avisa);
  RUN_TEST(test_lavado_solo_corrige_ph);
  RUN_TEST(test_secuenciador_a_pausa_b);
  RUN_TEST(test_secuenciador_cancelar_apaga_la_bomba);
  RUN_TEST(test_controlador_riega_y_registra_eventos);
  RUN_TEST(test_controlador_alarmas_al_aparecer_y_resolverse);
  RUN_TEST(test_controlador_cambiar_etapa_a_floracion);
  RUN_TEST(test_controlador_apagar_todo);
  RUN_TEST(test_controlador_prueba_manual_de_rele);
  RUN_TEST(test_bitacora_circular);
  RUN_TEST(test_calibracion_y_vpd);
  return UNITY_END();
}
