// Controlador de cultivo para ESP32: luz, humedad, riego y nutrientes.
// La configuración está en config.h; la lógica de control, en lib/cultivo.
#include <Arduino.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_timer.h>

#include <cultivo.h>

#include "config.h"
#include "hardware.h"
#include "panel_web.h"
#include "reloj.h"

using namespace cultivo;

namespace {

// Al arrancar se espera un poco a que el NTP ponga la hora antes del primer ciclo,
// para no apagar la luz por error mientras el WiFi conecta.
constexpr uint64_t ESPERA_HORA_AL_ARRANCAR_MS = 30 * 1000ULL;

Hardware hardware;
ControladorCultivo controlador(configCultivo(), ETAPA_INICIAL, hardware);
Preferences preferencias;
Lecturas lecturas;
PanelWeb panel(controlador, lecturas, hardware, preferencias);
bool primerCiclo = true;
uint64_t ultimoCiclo = 0;

uint64_t milisegundos() { return static_cast<uint64_t>(esp_timer_get_time() / 1000); }

void imprimirEvento(const char* texto) {
  char hora[24];
  formatearHora(hora, sizeof hora, milisegundos());
  Serial.printf("%s  %s\n", hora, texto);
}

void imprimirLecturas(const Lecturas& l) {
  char hora[24];
  formatearHora(hora, sizeof hora, l.ms);
  Serial.printf("%s  T %.1f °C  HR %.0f %%  sustrato %.0f %%  EC %.2f  pH %.2f  %.0f lx  depósito %s\n", hora,
                l.temperatura, l.humedadRelativa, l.humedadSuelo, l.ec, l.ph, l.lux,
                l.deposito == Nivel::Ok ? "ok" : l.deposito == Nivel::Bajo ? "BAJO" : "?");
}

void iniciarWifi() {
  if (WIFI_SSID[0] == '\0') {
    Serial.println("Sin WiFi configurado (src/secretos.h): sin panel web ni hora por NTP");
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(NOMBRE_HOST);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  configTzTime(ZONA_HORARIA, "pool.ntp.org", "time.google.com");
}

void vigilarWifi() {
  static bool conectado = false;
  static bool mdnsIniciado = false;
  const bool ahora = WiFi.status() == WL_CONNECTED;
  if (ahora && !conectado) {
    if (!mdnsIniciado && MDNS.begin(NOMBRE_HOST)) {
      MDNS.addService("http", "tcp", 80);
      mdnsIniciado = true;
    }
    Serial.printf("WiFi conectado. Panel: http://%s  o  http://%s.local\n", WiFi.localIP().toString().c_str(),
                  NOMBRE_HOST);
  } else if (!ahora && conectado) {
    Serial.println("WiFi desconectado: el control sigue funcionando, reintentando...");
  }
  conectado = ahora;
}

}  // namespace

void setup() {
  hardware.iniciar();  // lo primero: todos los relés apagados
  Serial.begin(115200);
  Serial.println("\nControlador de cultivo");

  controlador.alEvento(imprimirEvento);
  preferencias.begin("cultivo", false);
  const uint8_t etapa = preferencias.getUChar("etapa", static_cast<uint8_t>(ETAPA_INICIAL));
  if (etapa < NUM_ETAPAS) controlador.cambiarEtapa(static_cast<Etapa>(etapa));
  Serial.printf("Etapa: %s\n", nombre(controlador.etapa()));

  iniciarReloj();
  iniciarWifi();
  panel.iniciar();
  // Si el bucle se cuelga más de unos segundos, el ESP32 se reinicia y los relés se sueltan.
  enableLoopWDT();
}

void loop() {
  const uint64_t ms = milisegundos();
  controlador.atender(ms);  // termina los pulsos de bomba a su hora
  panel.atender();
  vigilarWifi();

  const bool esperandoHora = primerCiclo && !horaValida() && ms < ESPERA_HORA_AL_ARRANCAR_MS;
  if (!esperandoHora && (primerCiclo || ms - ultimoCiclo >= INTERVALO_CONTROL_MS)) {
    primerCiclo = false;
    ultimoCiclo = ms;
    Lecturas l;
    rellenarHora(l, ms);
    hardware.leer(l);
    controlador.ciclo(l);
    lecturas = l;
    imprimirLecturas(l);
    sincronizarRtc(ms);
  }
  delay(2);  // cede la CPU al WiFi
}
