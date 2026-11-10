#include "x4l.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <cstdlib>
#include <cstdio>
#include <cmath>

namespace esphome {
namespace x4l {

static const char *const TAG = "x4l";

static uint8_t pruefsumme(const char *von, const char *bis) {
  uint8_t x = 0;
  for (const char *p = von; p < bis; p++) x ^= (uint8_t) *p;
  return x;
}

void X4LComponent::loop() {
  while (this->available()) {
    uint8_t c;
    if (!this->read_byte(&c)) break;
    if (c == '\r') continue;
    if (c == '\n') {
      if (!this->puffer_.empty()) this->zeile_auswerten_(this->puffer_);
      this->puffer_.clear();
      continue;
    }
    if (this->puffer_.size() < 120) this->puffer_.push_back((char) c);
    else this->puffer_.clear();  // zu lang: verwerfen
  }
}

void X4LComponent::zeile_auswerten_(const std::string &z) {
  // Format: $....*HH
  size_t stern = z.rfind('*');
  if (z.size() < 5 || z[0] != '$' || stern == std::string::npos || stern + 3 != z.size()) {
    this->zeilen_fehler_++;
    ESP_LOGW(TAG, "Unbekanntes Zeilenformat verworfen: %s", z.c_str());
    return;
  }
  uint8_t erwartet = (uint8_t) strtoul(z.substr(stern + 1, 2).c_str(), nullptr, 16);
  if (pruefsumme(z.c_str() + 1, z.c_str() + stern) != erwartet) {
    this->zeilen_fehler_++;
    ESP_LOGW(TAG, "Pruefsumme falsch: %s", z.c_str());
    return;
  }
  std::string inhalt = z.substr(1, stern - 1);

  if (inhalt.rfind("V,", 0) == 0) {
    this->version_ = inhalt.substr(2);
    this->zeilen_ok_++;
    return;
  }
  if (inhalt.rfind("OK,", 0) == 0 || inhalt.rfind("ERR,", 0) == 0) {
    ESP_LOGI(TAG, "Trockner: %s", inhalt.c_str());
    this->zeilen_ok_++;
    return;
  }
  if (inhalt.rfind("K,", 0) == 0) {
    // 13 Zahlen: k, laeuft, soll, dauer, rest, material, t*100, rh*100, ntc*10, leistung, fehler, luefter, ok
    // (bis 10.11. stand hier faelschlich 14 - jede Zeile wurde still verworfen)
    static const int FELDER = 13;
    long w[FELDER];
    int n = 0;
    const char *p = inhalt.c_str() + 2;
    while (n < FELDER) {
      char *ende;
      w[n++] = strtol(p, &ende, 10);
      if (*ende == ',') p = ende + 1;
      else break;
    }
    if (n != FELDER || w[0] < 0 || w[0] > 1) {
      this->zeilen_fehler_++;
      ESP_LOGW(TAG, "Statuszeile mit %d statt %d Feldern verworfen: %s", n, FELDER, z.c_str());
      return;
    }
    KammerStatus &k = this->kammer_[w[0]];
    k.laeuft = w[1] != 0;
    k.soll = (int) w[2];
    k.dauer_min = (int) w[3];
    k.rest_s = (uint32_t) w[4];
    k.material = (int) w[5];
    k.sensor_ok = w[12] != 0;
    k.temperatur = k.sensor_ok ? w[6] / 100.0f : NAN;
    k.feuchte = k.sensor_ok ? w[7] / 100.0f : NAN;
    k.heizflaeche = w[8] == -32768 ? NAN : w[8] / 10.0f;
    k.leistung = (int) w[9];
    k.fehler = (int) w[10];
    k.luefter = w[11] != 0;
    k.gueltig = true;
    k.zeit_ms = millis();
    this->zeilen_ok_++;
    return;
  }
  this->zeilen_fehler_++;
}

bool X4LComponent::verbunden() const {
  uint32_t jetzt = millis();
  return this->kammer_[0].gueltig && jetzt - this->kammer_[0].zeit_ms < 5000;
}

void X4LComponent::befehl(const char *name, int kammer) {
  char z[48];
  int n = snprintf(z, sizeof(z), "!%s,%d", name, kammer & 1);
  snprintf(z + n, sizeof(z) - n, "*%02X\n", pruefsumme(z + 1, z + n));
  this->write_str(z);
  ESP_LOGI(TAG, "Befehl %s", z);
}

void X4LComponent::befehl(const char *name, int kammer, int wert) {
  char z[48];
  int n = snprintf(z, sizeof(z), "!%s,%d,%d", name, kammer & 1, wert);
  snprintf(z + n, sizeof(z) - n, "*%02X\n", pruefsumme(z + 1, z + n));
  this->write_str(z);
  ESP_LOGI(TAG, "Befehl %s", z);
}

void X4LComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "SpacePi X4L (eigene Firmware), Version Trockner: %s", this->version_.c_str());
  ESP_LOGCONFIG(TAG, "  Zeilen ok/fehlerhaft: %u/%u", (unsigned) this->zeilen_ok_, (unsigned) this->zeilen_fehler_);
}

// ---------- Thermostat je Kammer ----------

climate::ClimateTraits X4LClimate::traits() {
  auto t = climate::ClimateTraits();
  t.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE | climate::CLIMATE_SUPPORTS_CURRENT_HUMIDITY |
                      climate::CLIMATE_SUPPORTS_ACTION);
  t.add_supported_mode(climate::CLIMATE_MODE_OFF);
  t.add_supported_mode(climate::CLIMATE_MODE_HEAT);
  t.set_visual_min_temperature(35);
  t.set_visual_max_temperature(75);
  t.set_visual_temperature_step(1);
  return t;
}

void X4LClimate::control(const climate::ClimateCall &call) {
  if (this->parent_ == nullptr) return;
  if (call.get_target_temperature().has_value()) {
    int soll = (int) lroundf(*call.get_target_temperature());
    this->parent_->befehl("SOLL", this->kammer_, soll);
  }
  if (call.get_mode().has_value()) {
    if (*call.get_mode() == climate::CLIMATE_MODE_HEAT) this->parent_->befehl("START", this->kammer_);
    else this->parent_->befehl("STOP", this->kammer_);
  }
  // Der Zustand kommt mit der naechsten Statuszeile zurueck (loop()).
}

void X4LClimate::loop() {
  if (this->parent_ == nullptr) return;
  const KammerStatus &k = this->parent_->kammer(this->kammer_);
  if (!k.gueltig || millis() - this->letzte_ausgabe_ < 1000) return;
  this->letzte_ausgabe_ = millis();
  auto modus = k.laeuft ? climate::CLIMATE_MODE_HEAT : climate::CLIMATE_MODE_OFF;
  auto aktion = !k.laeuft ? climate::CLIMATE_ACTION_OFF
                          : (k.leistung > 0 ? climate::CLIMATE_ACTION_HEATING : climate::CLIMATE_ACTION_IDLE);
  // Zustand (Modus, Aktion, Soll) sofort melden. Messwerte nur bei Aenderung ab 0,2 K bzw. 1 %
  // und hoechstens alle 10 s - sonst schreibt der HA-Recorder jede Sekunde (10.11.2026).
  bool zustand = this->mode != modus || this->action != aktion || this->target_temperature != (float) k.soll;
  auto geaendert = [](float alt, float neu, float schwelle) {
    if (std::isnan(alt) != std::isnan(neu)) return true;
    return !std::isnan(neu) && fabsf(alt - neu) >= schwelle;
  };
  bool messwert = geaendert(this->current_temperature, k.temperatur, 0.2f) ||
                  geaendert(this->current_humidity, k.feuchte, 1.0f);
  uint32_t jetzt = millis();
  if (!zustand && !(messwert && jetzt - this->letzte_meldung_ >= 10000)) return;
  this->mode = modus;
  this->action = aktion;
  this->target_temperature = k.soll;
  this->current_temperature = k.temperatur;
  this->current_humidity = k.feuchte;
  this->letzte_meldung_ = jetzt;
  this->publish_state();
}

}  // namespace x4l
}  // namespace esphome
