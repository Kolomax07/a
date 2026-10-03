// Prueba de extremo a extremo: el controlador completo contra un armario virtual
// (el mismo modelo que la simulación de la versión Python).  pio test -e native
#include <cultivo.h>
#include <unity.h>

#include <algorithm>
#include <random>

using namespace cultivo;

namespace {

constexpr ObjetivosEtapa ETAPAS[NUM_ETAPAS] = {
    {18, 65, 75, 27, 0.6f}, {18, 50, 65, 28, 1.2f}, {12, 40, 50, 26, 1.6f},
    {12, 35, 45, 25, 1.4f}, {12, 35, 45, 25, 0.0f},
};

constexpr double TEMP_AMBIENTE = 21.0;
constexpr double HR_AMBIENTE = 45.0;
constexpr double CAUDAL_RIEGO_ML_S = 25.0;
constexpr double VOLUMEN_MINIMO_L = 5.0;

// Armario de cultivo virtual: la lámpara calienta, la planta transpira, el sustrato se
// seca, la EC baja y el pH sube; cada relé empuja su variable en la dirección esperada.
class Armario : public Salidas {
 public:
  double temperatura = 22, humedadRelativa = 55, humedadSuelo = 45, volumenL = 40, ec = 0.9, ph = 6.5;
  bool lamparaFundida = false;

  void fijar(Actuador a, bool encendido) override { reles_[static_cast<uint8_t>(a)] = encendido; }
  bool rele(Actuador a) const { return reles_[static_cast<uint8_t>(a)]; }

  void integrar(double dt, double caudalDosificadoras) {
    const double horas = dt / 3600;
    const bool luz = rele(Actuador::Luz) && !lamparaFundida;
    const bool extractor = rele(Actuador::Extractor);

    const double objetivo = TEMP_AMBIENTE + (extractor ? (luz ? 3 : 0) : (luz ? 8 : 0));
    temperatura += (objetivo - temperatura) * std::min(1.0, (extractor ? 3.0 : 1.0) * horas);

    double cambio = (luz ? 6.0 : 2.0) - (0.5 + (extractor ? 4.0 : 0.0)) * (humedadRelativa - HR_AMBIENTE);
    if (rele(Actuador::Humidificador)) cambio += 40;
    if (rele(Actuador::Deshumidificador)) cambio -= 40;
    humedadRelativa = std::min(99.0, std::max(15.0, humedadRelativa + cambio * horas));

    humedadSuelo -= (luz ? 2.0 : 0.6) * horas;
    if (rele(Actuador::BombaRiego) && volumenL > 0) {
      humedadSuelo += 0.8 * dt;
      volumenL -= CAUDAL_RIEGO_ML_S * dt / 1000;
    }
    humedadSuelo = std::min(100.0, std::max(0.0, humedadSuelo));

    ec = std::max(0.0, ec - 0.005 * horas);
    ph += 0.01 * horas;
    const double ml = caudalDosificadoras * dt;
    const double litros = std::max(volumenL, 1.0);
    for (Actuador parte : {Actuador::DosificadorA, Actuador::DosificadorB}) {
      if (rele(parte)) {
        ec += 0.5 * ml / litros;
        ph -= 0.4 * ml / litros;
      }
    }
    if (rele(Actuador::PhDown)) ph -= 4.0 * ml / litros;
    if (rele(Actuador::PhUp)) ph += 4.0 * ml / litros;
  }

  Lecturas leer(uint64_t ms) {
    std::normal_distribution<double> ruido(0, 1);
    Lecturas l;
    l.ms = ms;
    l.dia = static_cast<int32_t>(ms / 86400000ULL);
    l.minutoDelDia = static_cast<int16_t>(ms % 86400000ULL / 60000);
    l.temperatura = static_cast<float>(temperatura + 0.1 * ruido(rng_));
    l.humedadRelativa = static_cast<float>(humedadRelativa + 0.5 * ruido(rng_));
    l.humedadSuelo = static_cast<float>(humedadSuelo + 0.5 * ruido(rng_));
    const bool luz = rele(Actuador::Luz) && !lamparaFundida;
    l.lux = static_cast<float>(std::max(0.0, (luz ? 30000.0 : 0.0) + 20 * ruido(rng_)));
    l.ec = static_cast<float>(std::max(0.0, ec + 0.01 * ruido(rng_)));
    l.ph = static_cast<float>(ph + 0.01 * ruido(rng_));
    l.deposito = volumenL > VOLUMEN_MINIMO_L ? Nivel::Ok : Nivel::Bajo;
    return l;
  }

 private:
  bool reles_[NUM_ACTUADORES] = {};
  std::mt19937 rng_{1};
};

struct Resultado {
  double pctHrEnRango;
  float tempMax;
  float sueloMin;
  uint32_t riegos;
  double ecFinal;
  double phFinal;
  Alarmas alarmas;
  double horasDeLuz;
};

// Ejecuta el bucle igual que el firmware: atender() cada 100 ms y un ciclo cada 60 s.
Resultado simular(Etapa etapa, double dias, int16_t minutoEncendido = 6 * 60, Armario* externo = nullptr) {
  ConfigCultivo config;
  std::copy(std::begin(ETAPAS), std::end(ETAPAS), config.etapas);
  config.luz.minutoEncendido = minutoEncendido;
  const ObjetivosEtapa& o = ETAPAS[static_cast<uint8_t>(etapa)];

  Armario propio;
  Armario& armario = externo != nullptr ? *externo : propio;
  if (externo == nullptr) {  // depósito recién preparado, algo por debajo del objetivo
    armario.ec = o.ec > 0 ? o.ec - 0.3 : 0.4;
    armario.ph = config.rangoPh.max + 0.1;
  }
  ControladorCultivo controlador(config, etapa, armario);

  const uint64_t paso = 100, intervalo = 60000;
  const uint64_t fin = static_cast<uint64_t>(dias * 86400000.0);
  uint32_t lecturas = 0, enRango = 0, minutosDeLuz = 0;
  Resultado r{};
  r.tempMax = -1000;
  r.sueloMin = 1000;
  for (uint64_t ms = 0; ms < fin; ms += paso) {
    controlador.atender(ms);
    if (ms % intervalo == 0) {
      const Lecturas l = armario.leer(ms);
      controlador.ciclo(l);
      lecturas++;
      enRango += l.humedadRelativa >= o.hrMin && l.humedadRelativa <= o.hrMax;
      r.tempMax = std::max(r.tempMax, l.temperatura);
      r.sueloMin = std::min(r.sueloMin, l.humedadSuelo);
      minutosDeLuz += controlador.estado(Actuador::Luz);
    }
    armario.integrar(paso / 1000.0, config.nutrientes.caudalMlS);
  }
  r.pctHrEnRango = 100.0 * enRango / lecturas;
  r.riegos = controlador.pulsos(Actuador::BombaRiego);
  r.ecFinal = armario.ec;
  r.phFinal = armario.ph;
  r.alarmas = controlador.historialAlarmas();
  r.horasDeLuz = minutosDeLuz / 60.0 / dias;
  return r;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_tres_dias_de_vegetativo_dentro_de_objetivos() {
  const Resultado r = simular(Etapa::Vegetativo, 3);
  TEST_ASSERT_TRUE(r.pctHrEnRango > 95);
  TEST_ASSERT_TRUE(r.tempMax < 28 + 1);
  TEST_ASSERT_TRUE(r.sueloMin > 35 - 5);
  TEST_ASSERT_TRUE(r.riegos > 3);
  TEST_ASSERT_FLOAT_WITHIN(0.15, 1.2, r.ecFinal);
  TEST_ASSERT_TRUE(r.phFinal >= 5.7 && r.phFinal <= 6.3);
  TEST_ASSERT_EQUAL_UINT32(0, r.alarmas);
  TEST_ASSERT_FLOAT_WITHIN(0.1, 18, r.horasDeLuz);
}

void test_todas_las_etapas_sin_alarmas() {
  for (uint8_t i = 0; i < NUM_ETAPAS; i++) {
    const Resultado r = simular(static_cast<Etapa>(i), 2);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, r.alarmas, nombre(static_cast<Etapa>(i)));
  }
}

void test_floracion_12_horas_desde_las_20() {
  const Resultado r = simular(Etapa::Floracion, 2, 20 * 60);
  TEST_ASSERT_FLOAT_WITHIN(0.1, 12, r.horasDeLuz);
}

void test_lampara_fundida_genera_alarma() {
  Armario armario;
  armario.lamparaFundida = true;
  const Resultado r = simular(Etapa::Vegetativo, 0.5, 6 * 60, &armario);
  TEST_ASSERT_TRUE(r.alarmas & alarma::LAMPARA_NO_ENCIENDE);
}

void test_deposito_vacio_bloquea_riego_y_dosis() {
  Armario armario;
  armario.volumenL = 2;
  armario.humedadSuelo = 10;
  armario.ec = 0.2;
  const Resultado r = simular(Etapa::Vegetativo, 1, 6 * 60, &armario);
  TEST_ASSERT_EQUAL_UINT32(0, r.riegos);
  TEST_ASSERT_TRUE(r.alarmas & alarma::DEPOSITO_BAJO);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 2.0, armario.volumenL);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_tres_dias_de_vegetativo_dentro_de_objetivos);
  RUN_TEST(test_todas_las_etapas_sin_alarmas);
  RUN_TEST(test_floracion_12_horas_desde_las_20);
  RUN_TEST(test_lampara_fundida_genera_alarma);
  RUN_TEST(test_deposito_vacio_bloquea_riego_y_dosis);
  return UNITY_END();
}
