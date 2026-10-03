#include "cultivo_controladores.h"

namespace cultivo {

namespace {

constexpr int MINUTOS_DIA = 24 * 60;
constexpr float MARGEN_TEMPERATURA_CRITICA = 3.0f;  // °C sobre tempMax para avisar

// Fuera de estos rangos la lectura es físicamente imposible en un depósito de riego:
// la sonda está averiada, descalibrada o fuera del agua.
constexpr float EC_MAXIMA_VALIDA = 6.0f;
constexpr float EC_MINIMA_CON_NUTRIENTES = 0.05f;  // ~agua destilada: sonda al aire o depósito sin preparar
constexpr float PH_MINIMO_VALIDO = 2.0f;
constexpr float PH_MAXIMO_VALIDO = 12.0f;

bool conHisteresis(bool encendido, bool encender, bool apagar) {
  if (encender) return true;
  if (apagar) return false;
  return encendido;
}

}  // namespace

// --- Interruptor -------------------------------------------------------------

bool Interruptor::pedir(uint64_t ms, bool encender) {
  if (encender != encendido_ && (!haCambiado_ || ms - ultimoCambio_ >= tiempoMinimoMs_)) {
    encendido_ = encender;
    ultimoCambio_ = ms;
    haCambiado_ = true;
  }
  return encendido_;
}

void Interruptor::apagar(uint64_t ms) {
  if (encendido_) {
    encendido_ = false;
    ultimoCambio_ = ms;
    haCambiado_ = true;
  }
}

// --- Luz ---------------------------------------------------------------------

bool ControladorLuz::debeEstarEncendida(int minutoDelDia) const {
  int transcurrido = ((minutoDelDia - config_.minutoEncendido) % MINUTOS_DIA + MINUTOS_DIA) % MINUTOS_DIA;
  return transcurrido < horasLuz_ * 60;
}

bool ControladorLuz::actualizar(const Lecturas& l) {
  alarmas_ = 0;
  if (!l.horaValida) {
    // Sin hora no se sabe si es de día o de noche: luz apagada, que en floración
    // es menos dañino que encenderla en mitad del periodo de oscuridad.
    hayDiscrepancia_ = false;
    return false;
  }
  bool encendida = debeEstarEncendida(l.minutoDelDia);
  vigilarSensor(l, encendida);
  return encendida;
}

void ControladorLuz::vigilarSensor(const Lecturas& l, bool encendida) {
  if (std::isnan(l.lux)) {
    hayDiscrepancia_ = false;
    return;
  }
  bool hayLuz = l.lux >= config_.umbralLux;
  if (hayLuz == encendida) {
    hayDiscrepancia_ = false;
    return;
  }
  if (!hayDiscrepancia_) {
    hayDiscrepancia_ = true;
    discrepanciaDesde_ = l.ms;
  }
  if (l.ms - discrepanciaDesde_ >= config_.toleranciaMs) {
    alarmas_ |= encendida ? alarma::LAMPARA_NO_ENCIENDE : alarma::FUGA_DE_LUZ;
  }
}

// --- Humedad -----------------------------------------------------------------

ControladorHumedad::ControladorHumedad(const ConfigHumedad& config, const ObjetivosEtapa& objetivos)
    : config_(config),
      objetivos_(objetivos),
      humidificador_(config.tiempoMinimoCicloMs),
      deshumidificador_(config.tiempoMinimoCicloMs),
      extractor_(config.tiempoMinimoCicloMs) {}

EstadoClima ControladorHumedad::actualizar(const Lecturas& l) {
  alarmas_ = 0;
  const ObjetivosEtapa& o = objetivos_;
  const float h = config_.histeresis;
  const float hr = l.humedadRelativa;
  const float temperatura = l.temperatura;

  if (std::isnan(hr)) {
    alarmas_ |= alarma::SIN_HUMEDAD_RELATIVA;
    humidificador_.apagar(l.ms);
    deshumidificador_.apagar(l.ms);
  } else {
    bool quiereHumedad = conHisteresis(humidificador_.encendido(), hr < o.hrMin, hr >= o.hrMin + h);
    bool quiereSecar = conHisteresis(deshumidificador_.encendido(), hr > o.hrMax, hr <= o.hrMax - h);
    humidificador_.pedir(l.ms, quiereHumedad && !deshumidificador_.encendido());
    deshumidificador_.pedir(l.ms, quiereSecar && !humidificador_.encendido());
  }

  if (std::isnan(temperatura)) {
    alarmas_ |= alarma::SIN_TEMPERATURA;
    extractor_.pedir(l.ms, true);
  } else {
    const float ht = config_.histeresisTemp;
    bool sobraCalor = temperatura > o.tempMax;
    bool calorOk = temperatura <= o.tempMax - ht;
    bool sobraHumedad = !std::isnan(hr) && hr > o.hrMax;
    bool humedadOk = std::isnan(hr) || hr <= o.hrMax - h;
    extractor_.pedir(l.ms, conHisteresis(extractor_.encendido(), sobraCalor || sobraHumedad, calorOk && humedadOk));
    if (temperatura >= o.tempMax + MARGEN_TEMPERATURA_CRITICA) alarmas_ |= alarma::TEMPERATURA_CRITICA;
  }

  EstadoClima estado;
  estado.humidificador = humidificador_.encendido();
  estado.deshumidificador = deshumidificador_.encendido();
  estado.extractor = extractor_.encendido();
  return estado;
}

// --- Riego -------------------------------------------------------------------

uint32_t ControladorRiego::actualizar(const Lecturas& l, bool luzEncendida) {
  alarmas_ = 0;
  if (!hayDia_ || dia_ != l.dia) {
    hayDia_ = true;
    dia_ = l.dia;
    riegosHoy_ = 0;
  }

  const float suelo = l.humedadSuelo;
  if (std::isnan(suelo)) {
    alarmas_ |= alarma::SIN_HUMEDAD_SUSTRATO;
    return 0;
  }
  if (suelo >= config_.humedadSueloMin) return 0;
  if (haRegado_ && l.ms - ultimoRiego_ < config_.esperaAbsorcionMs) return 0;
  if (config_.soloConLuz && !luzEncendida) return 0;
  if (l.deposito == Nivel::Bajo) return 0;  // el aviso de depósito bajo lo da el controlador principal
  if (riegosHoy_ >= config_.maxRiegosDia) {
    alarmas_ |= alarma::LIMITE_RIEGOS;
    return 0;
  }

  haRegado_ = true;
  ultimoRiego_ = l.ms;
  riegosHoy_++;
  return config_.duracionMs;
}

// --- Nutrientes --------------------------------------------------------------

Dosis ControladorNutrientes::actualizar(const Lecturas& l) {
  alarmas_ = 0;
  if (!hayDia_ || dia_ != l.dia) {
    hayDia_ = true;
    dia_ = l.dia;
    dosisNutrientesHoy_ = 0;
    dosisPhHoy_ = 0;
  }

  Dosis dosis;
  if (l.deposito == Nivel::Bajo) return dosis;  // con nivel bajo las sondas pueden estar fuera del agua

  const ConfigNutrientes& c = config_;
  const bool manejaEc = ecObjetivo_ > 0;
  const float ec = l.ec;
  const float ph = l.ph;
  if (std::isnan(ec) || ec < 0 || ec > EC_MAXIMA_VALIDA || (manejaEc && ec < EC_MINIMA_CON_NUTRIENTES)) {
    alarmas_ |= alarma::EC_NO_VALIDA;
    return dosis;
  }
  if (std::isnan(ph) || ph < PH_MINIMO_VALIDO || ph > PH_MAXIMO_VALIDO) {
    alarmas_ |= alarma::PH_NO_VALIDO;
    return dosis;
  }
  if (manejaEc && ec > ecObjetivo_ + c.margenEcAlta) alarmas_ |= alarma::EC_ALTA;

  if (haDosificado_ && l.ms - ultimaDosis_ < c.esperaMezclaMs) return dosis;

  if (ec < ecObjetivo_ - c.toleranciaEc) {
    subiendoEc_ = manejaEc;
  } else if (ec >= ecObjetivo_) {
    subiendoEc_ = false;
  }

  const float centroPh = (rangoPh_.min + rangoPh_.max) / 2;
  if (ph > rangoPh_.max) {
    correccionPh_ = CorreccionPh::Bajar;
  } else if (ph < rangoPh_.min) {
    correccionPh_ = CorreccionPh::Subir;
  } else if ((correccionPh_ == CorreccionPh::Bajar && ph <= centroPh) ||
             (correccionPh_ == CorreccionPh::Subir && ph >= centroPh)) {
    correccionPh_ = CorreccionPh::Ninguna;
  }

  if (subiendoEc_) {
    if (dosisNutrientesHoy_ < c.maxDosisNutrientesDia) {
      dosisNutrientesHoy_++;
      haDosificado_ = true;
      ultimaDosis_ = l.ms;
      dosis.cantidad = 2;
      dosis.ms = static_cast<uint32_t>(c.mlPorDosis / c.caudalMlS * 1000);
      return dosis;
    }
    if (ec < ecObjetivo_ - c.toleranciaEc) alarmas_ |= alarma::LIMITE_NUTRIENTES;
    // sin dosis de nutrientes disponibles, al menos se mantiene el pH
  }

  if (correccionPh_ == CorreccionPh::Ninguna) return dosis;
  if (dosisPhHoy_ >= c.maxDosisPhDia) {
    if (ph < rangoPh_.min || ph > rangoPh_.max) alarmas_ |= alarma::LIMITE_PH;
    return dosis;
  }
  dosisPhHoy_++;
  haDosificado_ = true;
  ultimaDosis_ = l.ms;
  dosis.cantidad = 1;
  dosis.actuadores[0] = correccionPh_ == CorreccionPh::Bajar ? Actuador::PhDown : Actuador::PhUp;
  dosis.ms = static_cast<uint32_t>(c.mlPorDosisPh / c.caudalMlS * 1000);
  return dosis;
}

}  // namespace cultivo
