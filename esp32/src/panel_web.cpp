#include "panel_web.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>

#include "config.h"
#include "hardware.h"
#include "reloj.h"

using namespace cultivo;

namespace {

constexpr uint32_t DURACION_PRUEBA_MS = 2000;

WebServer servidor(80);

const char* const NOMBRES_ETAPA[NUM_ETAPAS] = {"Plántula", "Vegetativo", "Floración", "Floración tardía", "Lavado"};
const char* const NOMBRES_EQUIPO[NUM_ACTUADORES] = {
    "Luz", "Bomba de riego", "Humidificador", "Deshumidificador", "Extractor",
    "Nutriente A", "Nutriente B", "pH+", "pH-",
};

const char* const CSS =
    ":root{--fondo:#f3f6f2;--tarjeta:#fff;--texto:#1c2a1e;--suave:#5c6c5f;--verde:#2e7d32;--rojo:#c62828;"
    "--borde:#dce3db}"
    "@media(prefers-color-scheme:dark){:root{--fondo:#111712;--tarjeta:#1b231c;--texto:#e4ede4;--suave:#97a899;"
    "--verde:#66bb6a;--rojo:#e53935;--borde:#2b372c}}"
    "body{margin:0;padding:16px;font-family:system-ui,sans-serif;background:var(--fondo);color:var(--texto)}"
    "main{max-width:880px;margin:auto}h1{font-size:1.4rem;margin:0}"
    "h2{font-size:.85rem;margin:24px 0 8px;color:var(--suave);text-transform:uppercase;letter-spacing:.06em}"
    ".sub{color:var(--suave);margin:4px 0 16px}"
    ".rejilla{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:10px}"
    ".t{background:var(--tarjeta);border:1px solid var(--borde);border-radius:10px;padding:12px}"
    ".t b{display:block;font-size:1.45rem;margin:4px 0}.t span,.t .d{color:var(--suave);font-size:.8rem}"
    ".t form{margin-top:6px}.t button{padding:4px 10px;font-size:.8rem}"
    ".on{color:var(--verde)}.off{color:var(--suave);font-weight:400}"
    ".alarma{background:var(--rojo);color:#fff;border-radius:8px;padding:10px 12px;margin:10px 0}"
    "ul{list-style:none;padding:0;margin:0}li{padding:6px 0;border-bottom:1px solid var(--borde);font-size:.9rem}"
    "time{color:var(--suave);margin-right:8px;font-variant-numeric:tabular-nums}"
    "form{display:flex;gap:8px;flex-wrap:wrap}"
    "select,button{font:inherit;padding:8px 12px;border-radius:8px;border:1px solid var(--borde);"
    "background:var(--tarjeta);color:var(--texto)}button{background:var(--verde);border:0;color:#fff}"
    "a{color:var(--verde)}footer{margin:24px 0;color:var(--suave);font-size:.85rem}";

String num(float valor, unsigned int decimales, const char* unidad = "") {
  if (std::isnan(valor)) return String("—");
  String s(valor, decimales);
  s += unidad;
  return s;
}

String json(float valor, unsigned int decimales) {
  return std::isnan(valor) ? String("null") : String(valor, decimales);
}

// Valor convertido de una sonda, o aviso si no responde (las conversiones no propagan NAN).
String convertido(float voltaje, const String& valor) {
  if (std::isnan(voltaje)) return String("Sin respuesta");
  String s("Equivale a ");
  s += valor;
  return s;
}

String hora(int minutoDelDia) {
  char texto[6];
  snprintf(texto, sizeof texto, "%02d:%02d", (minutoDelDia / 60) % 24, minutoDelDia % 60);
  return String(texto);
}

void cabecera(String& h, const char* titulo, int refrescoS) {
  h += "<!doctype html><html lang=es><head><meta charset=utf-8>"
       "<meta name=viewport content='width=device-width,initial-scale=1'><meta http-equiv=refresh content=";
  h += refrescoS;
  h += "><title>";
  h += titulo;
  h += "</title><style>";
  h += CSS;
  h += "</style></head><body><main>";
}

void tarjeta(String& h, const char* titulo, const String& valor, const char* detalle, const char* clase = "") {
  h += "<div class=t><span>";
  h += titulo;
  h += "</span><b class='";
  h += clase;
  h += "'>";
  h += valor;
  h += "</b><div class=d>";
  h += detalle;
  h += "</div></div>";
}

}  // namespace

void PanelWeb::iniciar() {
  servidor.on("/", HTTP_GET, [this] { paginaPrincipal(); });
  servidor.on("/calibrar", HTTP_GET, [this] { paginaCalibrar(); });
  servidor.on("/api/estado", HTTP_GET, [this] { apiEstado(); });
  servidor.on("/etapa", HTTP_POST, [this] { cambiarEtapa(); });
  servidor.on("/probar", HTTP_POST, [this] { probarRele(); });
  servidor.onNotFound([] { servidor.send(404, "text/plain; charset=utf-8", "No encontrado"); });
  servidor.begin();
}

void PanelWeb::atender() { servidor.handleClient(); }

void PanelWeb::paginaPrincipal() {
  const Lecturas& l = lecturas_;
  const ConfigCultivo& c = controlador_.config();
  const ObjetivosEtapa& o = controlador_.objetivos();
  const RangoPh rango = controlador_.rangoPh();
  const uint8_t etapa = static_cast<uint8_t>(controlador_.etapa());
  char ahora[24];
  formatearHora(ahora, sizeof ahora, millis());
  char detalle[64];

  String h;
  h.reserve(8000);
  cabecera(h, "Cultivo", 30);
  const int finLuz = c.luz.minutoEncendido + static_cast<int>(o.horasLuz * 60);
  h += "<h1>Cultivo · ";
  h += NOMBRES_ETAPA[etapa];
  h += "</h1><p class=sub>";
  h += ahora;
  h += " · luz de " + hora(c.luz.minutoEncendido) + " a " + hora(finLuz) + "</p>";

  const Alarmas alarmas = controlador_.alarmas();
  for (uint8_t i = 0; i < alarma::TOTAL; i++) {
    if (!(alarmas & (1u << i))) continue;
    h += "<div class=alarma>";
    h += textoAlarma(i);
    h += "</div>";
  }

  h += "<h2>Lecturas</h2><div class=rejilla>";
  snprintf(detalle, sizeof detalle, "extractor por encima de %.0f °C", o.tempMax);
  tarjeta(h, "Temperatura", num(l.temperatura, 1, " °C"), detalle);
  snprintf(detalle, sizeof detalle, "objetivo %.0f–%.0f %%", o.hrMin, o.hrMax);
  tarjeta(h, "Humedad", num(l.humedadRelativa, 0, " %"), detalle);
  tarjeta(h, "VPD", num(vpd(l.temperatura, l.humedadRelativa), 2, " kPa"), "déficit de presión de vapor");
  snprintf(detalle, sizeof detalle, "riega por debajo de %.0f %%", c.riego.humedadSueloMin);
  tarjeta(h, "Sustrato", num(l.humedadSuelo, 0, " %"), detalle);
  tarjeta(h, "Luz", num(l.lux, 0, " lx"), controlador_.estado(Actuador::Luz) ? "lámpara encendida" : "lámpara apagada");
  if (o.ec > 0) {
    snprintf(detalle, sizeof detalle, "objetivo %.1f", o.ec);
  } else {
    snprintf(detalle, sizeof detalle, "lavado: sin nutrientes");
  }
  tarjeta(h, "EC", num(l.ec, 2, " mS/cm"), detalle);
  snprintf(detalle, sizeof detalle, "rango %.1f–%.1f", rango.min, rango.max);
  tarjeta(h, "pH", num(l.ph, 2), detalle);
  const char* deposito = l.deposito == Nivel::Ok ? "OK" : l.deposito == Nivel::Bajo ? "BAJO" : "—";
  tarjeta(h, "Depósito", String(deposito), "interruptor de nivel", l.deposito == Nivel::Bajo ? "" : "on");
  h += "</div>";

  h += "<h2>Equipos</h2><div class=rejilla>";
  for (uint8_t i = 0; i < NUM_ACTUADORES; i++) {
    if (PINES_RELES[i] < 0) continue;
    const bool encendido = controlador_.estado(static_cast<Actuador>(i));
    String prueba;
    if (!encendido) {  // pulso de 2 s para comprobar el cableado
      prueba = "<form method=post action=/probar><input type=hidden name=actuador value=";
      prueba += i;
      prueba += "><button>Probar</button></form>";
    }
    tarjeta(h, NOMBRES_EQUIPO[i], String(encendido ? "Encendido" : "Apagado"), prueba.c_str(),
            encendido ? "on" : "off");
  }
  h += "</div><h2>Hoy</h2><div class=rejilla>";
  snprintf(detalle, sizeof detalle, "máximo %u", c.riego.maxRiegosDia);
  tarjeta(h, "Riegos", String(controlador_.riegosHoy()), detalle);
  snprintf(detalle, sizeof detalle, "máximo %u", c.nutrientes.maxDosisNutrientesDia);
  tarjeta(h, "Dosis de nutrientes", String(controlador_.dosisNutrientesHoy()), detalle);
  snprintf(detalle, sizeof detalle, "máximo %u", c.nutrientes.maxDosisPhDia);
  tarjeta(h, "Correcciones de pH", String(controlador_.dosisPhHoy()), detalle);
  h += "</div>";

  h += "<h2>Etapa</h2><form method=post action=/etapa><select name=etapa>";
  for (uint8_t i = 0; i < NUM_ETAPAS; i++) {
    const ObjetivosEtapa& e = c.etapas[i];
    snprintf(detalle, sizeof detalle, " (%.0f h luz, HR %.0f–%.0f %%)", e.horasLuz, e.hrMin, e.hrMax);
    h += "<option value=";
    h += i;
    if (i == etapa) h += " selected";
    h += ">";
    h += NOMBRES_ETAPA[i];
    h += detalle;
    h += "</option>";
  }
  h += "</select><button>Cambiar</button></form>";

  h += "<h2>Últimos eventos</h2><ul>";
  const Bitacora& bitacora = controlador_.bitacora();
  for (uint8_t i = 0; i < bitacora.cantidad(); i++) {
    const Bitacora::Entrada& e = bitacora.entrada(i);
    h += "<li><time>";
    h += e.horaValida ? hora(e.minutoDelDia) : String("--:--");
    h += "</time>";
    h += e.texto;
    h += "</li>";
  }
  if (bitacora.cantidad() == 0) h += "<li>Sin eventos todavía</li>";
  h += "</ul><footer><a href=/calibrar>Calibrar sondas</a> · <a href=/api/estado>API JSON</a></footer>"
       "</main></body></html>";
  servidor.send(200, "text/html; charset=utf-8", h);
}

void PanelWeb::paginaCalibrar() {
  const CalibracionSondas cal = calibracionSondas();
  const float vSuelo = hardware_.voltaje(Sonda::Suelo);
  const float vEc = hardware_.voltaje(Sonda::Ec);
  const float vPh = hardware_.voltaje(Sonda::Ph);

  String h;
  h.reserve(3000);
  cabecera(h, "Calibrar sondas", 2);
  h += "<h1>Calibrar sondas</h1><p class=sub>Valores en vivo cada 2 s. Anota los voltajes en "
       "<code>calibracionSondas()</code> de <code>src/config.h</code> y vuelve a compilar.</p>"
       "<div class=rejilla>";
  String detalle;
  detalle = convertido(vSuelo, num(humedadSuelo(vSuelo, cal), 0, " %"));
  detalle += ". Al aire: sueloVSeco; en agua: sueloVMojado";
  tarjeta(h, "Sustrato", num(vSuelo, 3, " V"), detalle.c_str());
  detalle = convertido(vEc, num(ec(vEc, cal, lecturas_.temperatura), 2, " mS/cm"));
  detalle += ". En agua destilada: ecV0; en patrón 1,413: ecV1413";
  tarjeta(h, "EC", num(vEc, 3, " V"), detalle.c_str());
  detalle = convertido(vPh, num(ph(vPh, cal), 2));
  detalle += ". En tampón pH 7: phV7; en tampón pH 4: phV4";
  tarjeta(h, "pH", num(vPh, 3, " V"), detalle.c_str());
  h += "</div><footer><a href=/>Volver</a></footer></main></body></html>";
  servidor.send(200, "text/html; charset=utf-8", h);
}

void PanelWeb::apiEstado() {
  const Lecturas& l = lecturas_;
  String j;
  j.reserve(1500);
  j += "{\"etapa\":\"";
  j += nombre(controlador_.etapa());
  j += "\",\"hora_valida\":";
  j += l.horaValida ? "true" : "false";
  j += ",\"lecturas\":{\"temperatura\":" + json(l.temperatura, 1);
  j += ",\"humedad_relativa\":" + json(l.humedadRelativa, 1);
  j += ",\"vpd\":" + json(vpd(l.temperatura, l.humedadRelativa), 2);
  j += ",\"humedad_suelo\":" + json(l.humedadSuelo, 1);
  j += ",\"lux\":" + json(l.lux, 0);
  j += ",\"ec\":" + json(l.ec, 2);
  j += ",\"ph\":" + json(l.ph, 2);
  j += ",\"deposito_ok\":";
  j += l.deposito == Nivel::Desconocido ? "null" : l.deposito == Nivel::Ok ? "true" : "false";
  j += "},\"reles\":{";
  for (uint8_t i = 0; i < NUM_ACTUADORES; i++) {
    if (i) j += ",";
    j += "\"";
    j += nombre(static_cast<Actuador>(i));
    j += "\":";
    j += controlador_.estado(static_cast<Actuador>(i)) ? "true" : "false";
  }
  j += "},\"hoy\":{\"riegos\":";
  j += controlador_.riegosHoy();
  j += ",\"dosis_nutrientes\":";
  j += controlador_.dosisNutrientesHoy();
  j += ",\"dosis_ph\":";
  j += controlador_.dosisPhHoy();
  j += "},\"alarmas\":[";
  bool primera = true;
  for (uint8_t i = 0; i < alarma::TOTAL; i++) {
    if (!(controlador_.alarmas() & (1u << i))) continue;
    if (!primera) j += ",";
    primera = false;
    j += "\"";
    j += textoAlarma(i);
    j += "\"";
  }
  j += "]}";
  servidor.send(200, "application/json; charset=utf-8", j);
}

void PanelWeb::probarRele() {
  if (!autorizado()) return;
  const long actuador = servidor.arg("actuador").toInt();
  if (!servidor.hasArg("actuador") || actuador < 0 || actuador >= NUM_ACTUADORES) {
    servidor.send(400, "text/plain; charset=utf-8", "Equipo no válido");
    return;
  }
  if (!controlador_.probar(static_cast<Actuador>(actuador), DURACION_PRUEBA_MS)) {
    servidor.send(409, "text/plain; charset=utf-8", "Ahora no se puede: hay una bomba en marcha o el equipo ya está encendido");
    return;
  }
  servidor.sendHeader("Location", "/");
  servidor.send(303);
}

bool PanelWeb::autorizado() {
  if (PANEL_PASSWORD[0] == '\0' || servidor.authenticate("admin", PANEL_PASSWORD)) return true;
  servidor.requestAuthentication();
  return false;
}

void PanelWeb::cambiarEtapa() {
  if (!autorizado()) return;
  const long etapa = servidor.arg("etapa").toInt();
  if (!servidor.hasArg("etapa") || etapa < 0 || etapa >= NUM_ETAPAS) {
    servidor.send(400, "text/plain; charset=utf-8", "Etapa no válida");
    return;
  }
  controlador_.cambiarEtapa(static_cast<Etapa>(etapa));
  preferencias_.putUChar("etapa", static_cast<uint8_t>(etapa));
  servidor.sendHeader("Location", "/");
  servidor.send(303);
}
