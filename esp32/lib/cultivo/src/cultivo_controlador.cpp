#include "cultivo_controlador.h"

#include <cstdio>
#include <cstring>

namespace cultivo {

// --- Secuenciador ------------------------------------------------------------

bool Secuenciador::encolar(Actuador actuador, uint32_t msEncendido, uint32_t pausaDespuesMs) {
  if (cantidad_ == CAPACIDAD) return false;
  pasos_[(primero_ + cantidad_) % CAPACIDAD] = Paso{actuador, msEncendido, pausaDespuesMs};
  cantidad_++;
  return true;
}

void Secuenciador::atender(uint64_t ms, Salidas& salidas) {
  if (enMarcha_) {
    if (ms < finPaso_) return;
    const Paso& paso = pasos_[primero_];
    salidas.fijar(paso.actuador, false);
    libreDesde_ = ms + paso.pausaDespuesMs;
    enMarcha_ = false;
    primero_ = (primero_ + 1) % CAPACIDAD;
    cantidad_--;
  }
  if (cantidad_ > 0 && ms >= libreDesde_) {
    const Paso& paso = pasos_[primero_];
    salidas.fijar(paso.actuador, true);
    finPaso_ = ms + paso.msEncendido;
    enMarcha_ = true;
  }
}

void Secuenciador::cancelar(Salidas& salidas) {
  if (enMarcha_) salidas.fijar(pasos_[primero_].actuador, false);
  enMarcha_ = false;
  cantidad_ = 0;
}

// --- Bitácora ----------------------------------------------------------------

void Bitacora::anotar(const Lecturas& l, const char* texto) {
  Entrada& e = entradas_[siguiente_];
  e.horaValida = l.horaValida;
  e.minutoDelDia = l.minutoDelDia;
  std::strncpy(e.texto, texto, LARGO - 1);
  e.texto[LARGO - 1] = '\0';
  siguiente_ = (siguiente_ + 1) % CAPACIDAD;
  if (cantidad_ < CAPACIDAD) cantidad_++;
}

const Bitacora::Entrada& Bitacora::entrada(uint8_t i) const {
  return entradas_[(siguiente_ + CAPACIDAD - 1 - i) % CAPACIDAD];
}

// --- Controlador principal ---------------------------------------------------

void ControladorCultivo::Reles::fijar(Actuador a, bool encendido) {
  // Se escribe siempre (no solo al cambiar) para reafirmar el estado del relé.
  estados_[static_cast<uint8_t>(a)] = encendido;
  salidas_.fijar(a, encendido);
}

ControladorCultivo::ControladorCultivo(const ConfigCultivo& config, Etapa etapa, Salidas& salidas)
    : config_(config),
      etapa_(etapa),
      reles_(salidas, estados_),
      luz_(config.luz, objetivos().horasLuz),
      humedad_(config.humedad, objetivos()),
      riego_(config.riego),
      nutrientes_(config.nutrientes, objetivos().ec, config.rangoPh) {
  ultimas_.horaValida = false;
}

void ControladorCultivo::ciclo(const Lecturas& l) {
  ultimas_ = l;
  char texto[Bitacora::LARGO];

  const bool luz = luz_.actualizar(l);
  if (luz != estado(Actuador::Luz)) evento(l, luz ? "Luz encendida" : "Luz apagada");
  reles_.fijar(Actuador::Luz, luz);

  const EstadoClima clima = humedad_.actualizar(l);
  reles_.fijar(Actuador::Humidificador, clima.humidificador);
  reles_.fijar(Actuador::Deshumidificador, clima.deshumidificador);
  reles_.fijar(Actuador::Extractor, clima.extractor);

  // Riego y dosis solo cuando han terminado los pulsos del ciclo anterior.
  if (!secuenciador_.ocupado()) {
    // Sin hora no hay día ni noche: mejor regar a cualquier hora que dejar secar la planta.
    const uint32_t msRiego = riego_.actualizar(l, luz || !l.horaValida);
    if (msRiego > 0) {
      std::snprintf(texto, sizeof texto, "Riego %.0f s (sustrato al %.0f %%)", msRiego / 1000.0, l.humedadSuelo);
      evento(l, texto);
      pulso(Actuador::BombaRiego, msRiego, 0);
    }

    const Dosis dosis = nutrientes_.actualizar(l);
    if (dosis.cantidad > 0) {
      const char* que = dosis.cantidad == 2 ? "nutrientes A+B" : nombre(dosis.actuadores[0]);
      std::snprintf(texto, sizeof texto, "Dosis %s %.1f s (EC %.2f, pH %.2f)", que, dosis.ms / 1000.0, l.ec, l.ph);
      evento(l, texto);
      for (uint8_t i = 0; i < dosis.cantidad; i++) {
        const bool quedanMas = i + 1 < dosis.cantidad;
        pulso(dosis.actuadores[i], dosis.ms, quedanMas ? config_.nutrientes.pausaEntrePartesMs : 0);
      }
    }
  }

  Alarmas nuevas = luz_.alarmas() | humedad_.alarmas() | riego_.alarmas() | nutrientes_.alarmas();
  if (l.deposito == Nivel::Bajo) nuevas |= alarma::DEPOSITO_BAJO;
  if (!l.horaValida) nuevas |= alarma::HORA_NO_VALIDA;
  // Cada alarma se anota una vez al aparecer y otra al resolverse.
  for (uint8_t bit = 0; bit < alarma::TOTAL; bit++) {
    const Alarmas mascara = 1u << bit;
    const bool ahora = nuevas & mascara;
    const bool antes = alarmas_ & mascara;
    if (ahora == antes) continue;
    std::snprintf(texto, sizeof texto, "%s: %s", ahora ? "ALARMA" : "Resuelta", textoAlarma(bit));
    evento(l, texto);
  }
  alarmas_ = nuevas;
  historialAlarmas_ |= nuevas;

  secuenciador_.atender(l.ms, reles_);
}

void ControladorCultivo::atender(uint64_t ms) { secuenciador_.atender(ms, reles_); }

void ControladorCultivo::cambiarEtapa(Etapa etapa) {
  if (etapa == etapa_) return;
  etapa_ = etapa;
  const ObjetivosEtapa& o = objetivos();
  luz_.fijarHorasLuz(o.horasLuz);
  humedad_.fijarObjetivos(o);
  nutrientes_.fijarEcObjetivo(o.ec);
  char texto[Bitacora::LARGO];
  std::snprintf(texto, sizeof texto, "Etapa cambiada a %s", nombre(etapa));
  evento(ultimas_, texto);
}

bool ControladorCultivo::probar(Actuador a, uint32_t ms) {
  if (secuenciador_.ocupado() || estado(a)) return false;
  char texto[Bitacora::LARGO];
  std::snprintf(texto, sizeof texto, "Prueba manual: %s %.0f s", nombre(a), ms / 1000.0);
  evento(ultimas_, texto);
  return secuenciador_.encolar(a, ms, 0);
}

void ControladorCultivo::apagarTodo() {
  secuenciador_.cancelar(reles_);
  for (uint8_t i = 0; i < NUM_ACTUADORES; i++) reles_.fijar(static_cast<Actuador>(i), false);
}

void ControladorCultivo::evento(const Lecturas& l, const char* texto) {
  bitacora_.anotar(l, texto);
  if (alEvento_ != nullptr) alEvento_(texto);
}

void ControladorCultivo::pulso(Actuador a, uint32_t ms, uint32_t pausaDespuesMs) {
  pulsos_[static_cast<uint8_t>(a)]++;
  secuenciador_.encolar(a, ms, pausaDespuesMs);
}

}  // namespace cultivo
