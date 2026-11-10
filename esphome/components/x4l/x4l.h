// ESPHome component for the SpacePi X4L with custom firmware (1.3.0 or later).
// Reads the status lines "$K,..." from the UART header of the dryer and sends commands
// "!COMMAND,k[,value]*HH". Protocol see firmware/src/protocol.h.
#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/climate/climate.h"
#include <string>

namespace esphome {
namespace x4l {

struct ChamberStatus {
  bool valid{false};
  uint32_t time_ms{0};       // millis() of the last valid line
  bool running{false};
  int setpoint{0};
  int duration_min{0};
  uint32_t remaining_s{0};
  int material{0};
  float temperature{NAN};
  float humidity{NAN};
  float heater_surface{NAN};
  int power{0};
  int fault{0};
  bool fan{false};
  bool sensor_ok{false};
};

static const char *const MATERIALS[] = {"PLA", "PETG", "ABS", "ASA", "TPU", "PA", "PC", "USER"};
static const int MATERIAL_COUNT = 8;

class X4LComponent : public Component, public uart::UARTDevice {
 public:
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  const ChamberStatus &chamber(int k) const { return this->chamber_[k & 1]; }
  bool connected() const;                 // a valid line within the last 5 s
  const std::string &version() const { return this->version_; }
  uint32_t lines_ok() const { return this->lines_ok_; }
  uint32_t lines_rejected() const { return this->lines_rejected_; }

  void command(const char *name, int chamber);
  void command(const char *name, int chamber, int value);

 protected:
  void parse_line_(const std::string &z);
  std::string buffer_;
  ChamberStatus chamber_[2];
  std::string version_;
  uint32_t lines_ok_{0}, lines_rejected_{0};
};

class X4LClimate : public climate::Climate, public Component {
 public:
  void set_parent(X4LComponent *p) { this->parent_ = p; }
  void set_chamber(int k) { this->chamber_ = k; }
  void loop() override;
  climate::ClimateTraits traits() override;

 protected:
  void control(const climate::ClimateCall &call) override;
  X4LComponent *parent_{nullptr};
  int chamber_{0};
  uint32_t last_check_{0};
  uint32_t last_publish_{0};
};

}  // namespace x4l
}  // namespace esphome
