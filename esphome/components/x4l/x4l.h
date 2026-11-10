// ESPHome-Komponente fuer den SpacePi X4L mit eigener Firmware (ab 1.3.0).
// Liest die Statuszeilen "$K,..." von der UART-Leiste des Trockners und schickt Befehle
// "!BEFEHL,k[,wert]*HH". Protokoll siehe firmware/src/protokoll.h.
#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/climate/climate.h"
#include <string>

namespace esphome {
namespace x4l {

struct KammerStatus {
  bool gueltig{false};
  uint32_t zeit_ms{0};       // millis() der letzten gueltigen Zeile
  bool laeuft{false};
  int soll{0};
  int dauer_min{0};
  uint32_t rest_s{0};
  int material{0};
  float temperatur{NAN};
  float feuchte{NAN};
  float heizflaeche{NAN};
  int leistung{0};
  int fehler{0};
  bool luefter{false};
  bool sensor_ok{false};
};

static const char *const MATERIALIEN[] = {"PLA", "PETG", "ABS", "ASA", "TPU", "PA", "PC", "USER"};
static const int ANZAHL_MATERIALIEN = 8;

class X4LComponent : public Component, public uart::UARTDevice {
 public:
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  const KammerStatus &kammer(int k) const { return this->kammer_[k & 1]; }
  bool verbunden() const;                 // in den letzten 5 s eine gueltige Zeile
  const std::string &version() const { return this->version_; }
  uint32_t zeilen_ok() const { return this->zeilen_ok_; }
  uint32_t zeilen_fehler() const { return this->zeilen_fehler_; }

  void befehl(const char *name, int kammer);
  void befehl(const char *name, int kammer, int wert);

 protected:
  void zeile_auswerten_(const std::string &z);
  std::string puffer_;
  KammerStatus kammer_[2];
  std::string version_;
  uint32_t zeilen_ok_{0}, zeilen_fehler_{0};
};

class X4LClimate : public climate::Climate, public Component {
 public:
  void set_parent(X4LComponent *p) { this->parent_ = p; }
  void set_kammer(int k) { this->kammer_ = k; }
  void loop() override;
  climate::ClimateTraits traits() override;

 protected:
  void control(const climate::ClimateCall &call) override;
  X4LComponent *parent_{nullptr};
  int kammer_{0};
  uint32_t letzte_ausgabe_{0};
  uint32_t letzte_meldung_{0};
};

}  // namespace x4l
}  // namespace esphome
