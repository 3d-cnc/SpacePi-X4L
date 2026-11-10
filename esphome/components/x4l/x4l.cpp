#include "x4l.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <cstdlib>
#include <cstdio>
#include <cmath>

namespace esphome {
namespace x4l {

static const char *const TAG = "x4l";

static uint8_t checksum(const char *from, const char *to) {
  uint8_t x = 0;
  for (const char *p = from; p < to; p++) x ^= (uint8_t) *p;
  return x;
}

void X4LComponent::loop() {
  while (this->available()) {
    uint8_t c;
    if (!this->read_byte(&c)) break;
    if (c == '\r') continue;
    if (c == '\n') {
      if (!this->buffer_.empty()) this->parse_line_(this->buffer_);
      this->buffer_.clear();
      continue;
    }
    if (this->buffer_.size() < 120) this->buffer_.push_back((char) c);
    else this->buffer_.clear();  // too long: discard
  }
}

void X4LComponent::parse_line_(const std::string &z) {
  // Format: $....*HH
  size_t star = z.rfind('*');
  if (z.size() < 5 || z[0] != '$' || star == std::string::npos || star + 3 != z.size()) {
    this->lines_rejected_++;
    ESP_LOGW(TAG, "Unknown line format, discarded: %s", z.c_str());
    return;
  }
  uint8_t expected = (uint8_t) strtoul(z.substr(star + 1, 2).c_str(), nullptr, 16);
  if (checksum(z.c_str() + 1, z.c_str() + star) != expected) {
    this->lines_rejected_++;
    ESP_LOGW(TAG, "Wrong checksum: %s", z.c_str());
    return;
  }
  std::string body = z.substr(1, star - 1);

  if (body.rfind("V,", 0) == 0) {
    this->version_ = body.substr(2);
    this->lines_ok_++;
    return;
  }
  if (body.rfind("OK,", 0) == 0 || body.rfind("ERR,", 0) == 0) {
    ESP_LOGI(TAG, "Dryer: %s", body.c_str());
    this->lines_ok_++;
    return;
  }
  if (body.rfind("K,", 0) == 0) {
    // 13 numbers: k, running, setpoint, duration, remaining, material, t*100, rh*100, ntc*10,
    // power, fault, fan, sensor_ok
    static const int FIELDS = 13;
    long w[FIELDS];
    int n = 0;
    const char *p = body.c_str() + 2;
    while (n < FIELDS) {
      char *end;
      w[n++] = strtol(p, &end, 10);
      if (*end == ',') p = end + 1;
      else break;
    }
    if (n != FIELDS || w[0] < 0 || w[0] > 1) {
      this->lines_rejected_++;
      ESP_LOGW(TAG, "Status line with %d instead of %d fields discarded: %s", n, FIELDS, z.c_str());
      return;
    }
    ChamberStatus &k = this->chamber_[w[0]];
    k.running = w[1] != 0;
    k.setpoint = (int) w[2];
    k.duration_min = (int) w[3];
    k.remaining_s = (uint32_t) w[4];
    k.material = (int) w[5];
    k.sensor_ok = w[12] != 0;
    k.temperature = k.sensor_ok ? w[6] / 100.0f : NAN;
    k.humidity = k.sensor_ok ? w[7] / 100.0f : NAN;
    k.heater_surface = w[8] == -32768 ? NAN : w[8] / 10.0f;
    k.power = (int) w[9];
    k.fault = (int) w[10];
    k.fan = w[11] != 0;
    k.valid = true;
    k.time_ms = millis();
    this->lines_ok_++;
    return;
  }
  this->lines_rejected_++;
}

bool X4LComponent::connected() const {
  uint32_t now = millis();
  return this->chamber_[0].valid && now - this->chamber_[0].time_ms < 5000;
}

void X4LComponent::command(const char *name, int chamber) {
  char z[48];
  int n = snprintf(z, sizeof(z), "!%s,%d", name, chamber & 1);
  snprintf(z + n, sizeof(z) - n, "*%02X\n", checksum(z + 1, z + n));
  this->write_str(z);
  ESP_LOGI(TAG, "Command %s", z);
}

void X4LComponent::command(const char *name, int chamber, int value) {
  char z[48];
  int n = snprintf(z, sizeof(z), "!%s,%d,%d", name, chamber & 1, value);
  snprintf(z + n, sizeof(z) - n, "*%02X\n", checksum(z + 1, z + n));
  this->write_str(z);
  ESP_LOGI(TAG, "Command %s", z);
}

void X4LComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "SpacePi X4L (custom firmware), dryer version: %s", this->version_.c_str());
  ESP_LOGCONFIG(TAG, "  Lines ok/rejected: %u/%u", (unsigned) this->lines_ok_, (unsigned) this->lines_rejected_);
}

// ---------- thermostat per chamber ----------

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
    int setpoint = (int) lroundf(*call.get_target_temperature());
    this->parent_->command("SOLL", this->chamber_, setpoint);   // SOLL = setpoint (protocol token)
  }
  if (call.get_mode().has_value()) {
    if (*call.get_mode() == climate::CLIMATE_MODE_HEAT) this->parent_->command("START", this->chamber_);
    else this->parent_->command("STOP", this->chamber_);
  }
  // The new state comes back with the next status line (loop()).
}

void X4LClimate::loop() {
  if (this->parent_ == nullptr) return;
  const ChamberStatus &k = this->parent_->chamber(this->chamber_);
  if (!k.valid || millis() - this->last_check_ < 1000) return;
  this->last_check_ = millis();
  auto mode = k.running ? climate::CLIMATE_MODE_HEAT : climate::CLIMATE_MODE_OFF;
  auto action = !k.running ? climate::CLIMATE_ACTION_OFF
                           : (k.power > 0 ? climate::CLIMATE_ACTION_HEATING : climate::CLIMATE_ACTION_IDLE);
  // Report state changes (mode, action, setpoint) at once. Readings only on a change of at least
  // 0.2 K or 1 % and at most every 10 s - otherwise the HA recorder writes every second.
  bool state = this->mode != mode || this->action != action || this->target_temperature != (float) k.setpoint;
  auto changed = [](float old_value, float new_value, float threshold) {
    if (std::isnan(old_value) != std::isnan(new_value)) return true;
    return !std::isnan(new_value) && fabsf(old_value - new_value) >= threshold;
  };
  bool reading = changed(this->current_temperature, k.temperature, 0.2f) ||
                 changed(this->current_humidity, k.humidity, 1.0f);
  uint32_t now = millis();
  if (!state && !(reading && now - this->last_publish_ >= 10000)) return;
  this->mode = mode;
  this->action = action;
  this->target_temperature = k.setpoint;
  this->current_temperature = k.temperature;
  this->current_humidity = k.humidity;
  this->last_publish_ = now;
  this->publish_state();
}

}  // namespace x4l
}  // namespace esphome
