#pragma once

#include "thessla_airpack.h"

#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#ifdef USE_SELECT
#include "esphome/components/select/select.h"
#endif
#ifdef USE_NUMBER
#include "esphome/components/number/number.h"
#endif
#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif
#ifdef USE_BUTTON
#include "esphome/components/button/button.h"
#endif
#ifdef USE_TEXT
#include "esphome/components/text/text.h"
#endif

#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace esphome::thessla_airpack {

/// Common plumbing: parent pointer and the register an entity is bound to.
class ThesslaBound : public ThesslaEntity {
 public:
  void set_parent(ThesslaAirpack *parent) { this->parent_ = parent; }
  void set_register(EntityType space, uint16_t address) {
    this->space_ = space;
    this->address_ = address;
  }

 protected:
  std::optional<uint16_t> raw_() const { return this->parent_->get(this->space_, this->address_); }

  ThesslaAirpack *parent_{nullptr};
  EntityType space_{EntityType::HOLDING};
  uint16_t address_{0};
};

/// Ordered code -> label table shared by text sensors and selects.
class ThesslaOptions {
 public:
  void add_option(uint16_t code, const char *label, bool writable = false) {
    this->codes_.push_back(code);
    this->labels_.push_back(label);
    this->writable_.push_back(writable);
  }

 protected:
  std::optional<size_t> index_of_code_(uint16_t code) const {
    for (size_t i = 0; i < this->codes_.size(); i++) {
      if (this->codes_[i] == code)
        return i;
    }
    return std::nullopt;
  }
  std::vector<uint16_t> codes_;
  std::vector<const char *> labels_;
  std::vector<bool> writable_;
};

// ---------------------------------------------------------------------------
#ifdef USE_SENSOR
enum class SensorKind : uint8_t { REGISTER, CURRENT_INTENSITY };

class ThesslaSensor : public sensor::Sensor, public ThesslaBound {
 public:
  void set_kind(SensorKind kind) { this->kind_ = kind; }
  void set_scale(float scale) { this->scale_ = scale; }
  void set_signed(bool is_signed) { this->signed_ = is_signed; }
  void set_nan_raw(uint16_t raw) {
    this->nan_raw_ = raw;
    this->has_nan_raw_ = true;
  }

  void refresh() override {
    float value = NAN;
    if (this->kind_ == SensorKind::CURRENT_INTENSITY) {
      value = this->current_intensity_();
    } else if (auto raw = this->raw_()) {
      if (!(this->has_nan_raw_ && *raw == this->nan_raw_)) {
        float base = this->signed_ ? static_cast<float>(static_cast<int16_t>(*raw)) : static_cast<float>(*raw);
        value = base * this->scale_;
      }
    }
    if (std::isnan(value) && std::isnan(this->state))
      return;
    if (!std::isnan(value) && this->has_state() && value == this->state)
      return;
    this->publish_state(value);
  }

 protected:
  float current_intensity_() const {
    auto mode = this->parent_->operating_mode();
    std::optional<uint16_t> raw;
    if (mode == MODE_MANUAL) {
      raw = this->parent_->get(EntityType::HOLDING, REG_MANUAL_INTENSITY);
    } else if (mode == MODE_TEMPORARY) {
      raw = this->parent_->get(EntityType::HOLDING, REG_TEMP_INTENSITY);
    } else {
      raw = this->parent_->get(EntityType::INPUT_REGISTER, REG_SUPPLY_PERCENT);
    }
    if (!raw.has_value() || *raw > 150)
      return NAN;
    return static_cast<float>(*raw);
  }

  SensorKind kind_{SensorKind::REGISTER};
  float scale_{1.0f};
  bool signed_{false};
  bool has_nan_raw_{false};
  uint16_t nan_raw_{0};
};
#endif

// ---------------------------------------------------------------------------
#ifdef USE_BINARY_SENSOR
enum class BinaryKind : uint8_t { REGISTER, ONLINE };

class ThesslaBinarySensor : public binary_sensor::BinarySensor, public ThesslaBound {
 public:
  void set_kind(BinaryKind kind) { this->kind_ = kind; }

  void refresh() override {
    if (this->kind_ == BinaryKind::ONLINE) {
      this->publish_state(this->parent_->is_online());
      return;
    }
    auto raw = this->raw_();
    if (!raw.has_value()) {
      if (this->has_state())
        this->invalidate_state();
      return;
    }
    this->publish_state(*raw != 0);
  }

 protected:
  BinaryKind kind_{BinaryKind::REGISTER};
};
#endif

// ---------------------------------------------------------------------------
#ifdef USE_TEXT_SENSOR
enum class TextKind : uint8_t { ENUM, FIRMWARE, SERIAL_NUMBER, CLOCK, PACKED_DATE, SCHEDULE };

class ThesslaTextSensor : public text_sensor::TextSensor, public ThesslaBound, public ThesslaOptions {
 public:
  void set_kind(TextKind kind) { this->kind_ = kind; }
  /// For TextKind::SCHEDULE: 0 summer, 1 winter.
  void set_season(uint8_t season) { this->season_ = season; }

  void refresh() override {
    std::string value;
    switch (this->kind_) {
      case TextKind::ENUM:
        value = this->enum_text_();
        break;
      case TextKind::FIRMWARE:
        value = this->firmware_();
        break;
      case TextKind::SERIAL_NUMBER:
        value = this->serial_();
        break;
      case TextKind::CLOCK:
        value = this->clock_();
        break;
      case TextKind::PACKED_DATE:
        value = this->packed_date_();
        break;
      case TextKind::SCHEDULE:
        value = this->schedule_();
        break;
    }
    if (value.empty() || (this->has_state() && value == this->get_state()))
      return;
    this->publish_state(value);
  }

 protected:
  std::string enum_text_() const {
    auto raw = this->raw_();
    if (!raw.has_value())
      return {};
    if (auto index = this->index_of_code_(*raw))
      return this->labels_[*index];
    return "Nieznany (" + to_string(*raw) + ")";
  }
  std::string firmware_() const {
    auto major = this->parent_->get(EntityType::INPUT_REGISTER, 0);
    auto minor = this->parent_->get(EntityType::INPUT_REGISTER, 1);
    auto patch = this->parent_->get(EntityType::INPUT_REGISTER, 4);
    if (!major.has_value() || !minor.has_value())
      return {};
    char buf[24];
    if (patch.has_value()) {
      snprintf(buf, sizeof(buf), "%u.%02u.%u", *major, *minor, *patch);
    } else {
      snprintf(buf, sizeof(buf), "%u.%02u", *major, *minor);
    }
    return buf;
  }
  std::string serial_() const {
    std::string out;
    for (uint16_t i = 0; i < 6; i++) {
      auto word = this->parent_->get(EntityType::INPUT_REGISTER, 24 + i);
      if (!word.has_value() || *word > 0xFF)
        return {};
      char buf[3];
      snprintf(buf, sizeof(buf), "%02x", *word);
      out += buf;
    }
    if (out == "000000000000" || out == "ffffffffffff")
      return {};
    return out;
  }
  std::string clock_() const {
    uint16_t r[3];
    for (uint16_t i = 0; i < 3; i++) {
      auto word = this->parent_->get(EntityType::HOLDING, REG_CLOCK + i);
      if (!word.has_value())
        return {};
      r[i] = *word;
    }
    // BCD byte -> 0..99, or 255 when a nibble is not a decimal digit (unset / corrupted clock).
    auto bcd = [](uint8_t v) -> uint8_t {
      if ((v >> 4) > 9 || (v & 0x0F) > 9)
        return 255;
      return (v >> 4) * 10 + (v & 0x0F);
    };
    const uint8_t year = bcd(r[0] >> 8), month = bcd(r[0] & 0xFF), day = bcd(r[1] >> 8);
    const uint8_t hour = bcd(r[2] >> 8), minute = bcd(r[2] & 0xFF);
    if (year > 99 || month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59)
      return {};
    char buf[24];
    snprintf(buf, sizeof(buf), "20%02u-%02u-%02u %02u:%02u", year, month, day, hour, minute);
    return buf;
  }

  // Filter replacement dates: day b0-b4, month b5-b8, year-2000 b9-b15.
  std::string packed_date_() const {
    auto raw = this->raw_();
    if (!raw.has_value())
      return {};
    unsigned day = *raw & 0x1F, month = (*raw >> 5) & 0x0F, year = 2000 + (*raw >> 9);
    if (day < 1 || day > 31 || month < 1 || month > 12)
      return {};
    char buf[12];
    snprintf(buf, sizeof(buf), "%04u-%02u-%02u", year, month, day);
    return buf;
  }

  // A whole season as raw registers in hex, Monday first: per day 4 slot starts [GGMM], 4 slot settings
  // [AATT] and the airing start [GGMM] - 7 x 9 x 4 = 252 characters, within Home Assistant's 255.
  std::string schedule_() const {
    std::string out;
    out.reserve(252);
    char buf[5];
    for (uint8_t day = 0; day < 7; day++) {
      uint16_t regs[9];
      for (uint8_t k = 0; k < 4; k++) {
        auto start = this->parent_->get(EntityType::HOLDING, schedule_start_reg(this->season_, day, k));
        auto setting = this->parent_->get(EntityType::HOLDING, schedule_setting_reg(this->season_, day, k));
        if (!start.has_value() || !setting.has_value())
          return {};
        regs[k] = *start;
        regs[4 + k] = *setting;
      }
      auto airing = this->parent_->get(EntityType::HOLDING, schedule_airing_reg(this->season_, day));
      if (!airing.has_value())
        return {};
      regs[8] = *airing;
      for (uint16_t reg : regs) {
        snprintf(buf, sizeof(buf), "%04X", reg);
        out += buf;
      }
    }
    return out;
  }

  TextKind kind_{TextKind::ENUM};
  uint8_t season_{0};
};
#endif

// ---------------------------------------------------------------------------
#ifdef USE_TEXT
/// Write-only command input for the weekly schedule (see ThesslaAirpack::set_schedule). Its state shows
/// the outcome of the last command.
class ThesslaScheduleCommand : public text::Text, public ThesslaBound {
 public:
  void refresh() override {}

 protected:
  void control(const std::string &value) override {
    const std::string error = this->parent_->set_schedule(value);
    if (error.empty()) {
      this->publish_state("OK: " + value);
    } else {
      ESP_LOGW("thessla_airpack", "Schedule command '%s' refused: %s", value.c_str(), error.c_str());
      this->publish_state("Błąd: " + error);
    }
  }
};
#endif

// ---------------------------------------------------------------------------
#ifdef USE_SELECT
enum class SelectKind : uint8_t { DIRECT, OPERATING_MODE };

class ThesslaSelect : public select::Select, public ThesslaBound, public ThesslaOptions {
 public:
  void set_kind(SelectKind kind) { this->kind_ = kind; }

  void refresh() override {
    auto raw = this->raw_();
    if (!raw.has_value())
      return;
    auto index = this->index_of_code_(*raw);
    if (!index.has_value())
      return;
    if (this->has_state() && this->active_index() == index)
      return;
    this->publish_state(*index);
  }

 protected:
  void control(size_t index) override {
    if (index >= this->codes_.size())
      return;
    if (!this->writable_[index]) {
      ESP_LOGW("thessla_airpack", "'%s' cannot be set from here; it is triggered by the controller's inputs",
               this->labels_[index]);
      this->refresh_back_();
      return;
    }
    const uint16_t code = this->codes_[index];
    if (this->kind_ == SelectKind::OPERATING_MODE && code == MODE_TEMPORARY) {
      // Temporary mode needs an intensity: keep the one currently in effect.
      auto current = this->parent_->get(EntityType::HOLDING, REG_MANUAL_INTENSITY);
      if (this->parent_->operating_mode() == MODE_TEMPORARY)
        current = this->parent_->get(EntityType::HOLDING, REG_TEMP_INTENSITY);
      this->parent_->start_temporary_intensity(current.value_or(50));
      return;
    }
    this->parent_->write_register(this->address_, code);
  }

  void refresh_back_() {
    auto raw = this->raw_();
    if (raw.has_value()) {
      if (auto index = this->index_of_code_(*raw))
        this->publish_state(*index);
    }
  }

  SelectKind kind_{SelectKind::DIRECT};
};
#endif

// ---------------------------------------------------------------------------
#ifdef USE_NUMBER
enum class NumberKind : uint8_t { DIRECT, INTENSITY, SUPPLY_TEMPERATURE };

class ThesslaNumber : public number::Number, public ThesslaBound {
 public:
  void set_kind(NumberKind kind) { this->kind_ = kind; }
  void set_scale(float scale) { this->scale_ = scale; }

  void refresh() override {
    std::optional<uint16_t> raw;
    float scale = this->scale_;
    auto mode = this->parent_->operating_mode();
    switch (this->kind_) {
      case NumberKind::DIRECT:
        raw = this->raw_();
        break;
      case NumberKind::INTENSITY:
        scale = 1.0f;
        if (mode == MODE_TEMPORARY) {
          raw = this->parent_->get(EntityType::HOLDING, REG_TEMP_INTENSITY);
        } else if (mode == MODE_MANUAL) {
          raw = this->parent_->get(EntityType::HOLDING, REG_MANUAL_INTENSITY);
        } else {
          raw = this->parent_->get(EntityType::INPUT_REGISTER, REG_SUPPLY_PERCENT);
          if (!raw.has_value())
            raw = this->parent_->get(EntityType::HOLDING, REG_MANUAL_INTENSITY);
        }
        break;
      case NumberKind::SUPPLY_TEMPERATURE:
        scale = 0.5f;
        raw = this->parent_->get(EntityType::HOLDING,
                                 mode == MODE_TEMPORARY ? REG_TEMP_TEMPERATURE : REG_MANUAL_TEMPERATURE);
        break;
    }
    if (!raw.has_value())
      return;
    float value = static_cast<float>(*raw) * scale;
    if (this->has_state() && value == this->state)
      return;
    this->publish_state(value);
  }

 protected:
  void control(float value) override {
    switch (this->kind_) {
      case NumberKind::DIRECT:
        this->parent_->write_register(this->address_, static_cast<uint16_t>(lroundf(value / this->scale_)));
        break;
      case NumberKind::INTENSITY:
        this->parent_->set_intensity(static_cast<uint8_t>(lroundf(value)));
        break;
      case NumberKind::SUPPLY_TEMPERATURE:
        this->parent_->set_supply_temperature(value);
        break;
    }
  }

  NumberKind kind_{NumberKind::DIRECT};
  float scale_{1.0f};
};
#endif

// ---------------------------------------------------------------------------
#ifdef USE_SWITCH
class ThesslaSwitch : public switch_::Switch, public ThesslaBound {
 public:
  /// The controller stores some options as "disabled" flags; the switch then shows "enabled".
  void set_disable_flag(bool disable_flag) { this->disable_flag_ = disable_flag; }

  void refresh() override {
    auto raw = this->raw_();
    if (!raw.has_value())
      return;
    bool on = (*raw != 0) != this->disable_flag_;
    if (this->has_state() && this->state == on)
      return;
    this->publish_state(on);
  }

 protected:
  void write_state(bool state) override {
    this->parent_->write_register(this->address_, (state != this->disable_flag_) ? 1 : 0);
  }

  bool disable_flag_{false};
};
#endif

// ---------------------------------------------------------------------------
#ifdef USE_BUTTON
enum class ButtonKind : uint8_t { SYNC_CLOCK, WRITE };

class ThesslaButton : public button::Button, public ThesslaBound {
 public:
  void set_kind(ButtonKind kind) { this->kind_ = kind; }
  /// For ButtonKind::WRITE: the command value written to the bound register.
  void set_value(uint16_t value) { this->value_ = value; }
  void refresh() override {}

 protected:
  void press_action() override {
    switch (this->kind_) {
      case ButtonKind::SYNC_CLOCK:
        this->parent_->sync_clock();
        break;
      case ButtonKind::WRITE:
        this->parent_->write_register(this->address_, this->value_);
        break;
    }
  }

  ButtonKind kind_{ButtonKind::SYNC_CLOCK};
  uint16_t value_{0};
};
#endif

}  // namespace esphome::thessla_airpack
