// Panel web: estado en vivo, cambio de etapa, calibración de sondas y API JSON.
#pragma once

#include <cultivo.h>

class Hardware;
class Preferences;

class PanelWeb {
 public:
  PanelWeb(cultivo::ControladorCultivo& controlador, const cultivo::Lecturas& lecturas, Hardware& hardware,
           Preferences& preferencias)
      : controlador_(controlador), lecturas_(lecturas), hardware_(hardware), preferencias_(preferencias) {}
  void iniciar();
  void atender();

 private:
  void paginaPrincipal();
  void paginaCalibrar();
  void apiEstado();
  void cambiarEtapa();
  void probarRele();
  bool autorizado();  // pide la contraseña del panel si hay una configurada

  cultivo::ControladorCultivo& controlador_;
  const cultivo::Lecturas& lecturas_;
  Hardware& hardware_;
  Preferences& preferencias_;
};
