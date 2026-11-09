#include "thessla_airpack.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cmath>

namespace esphome::thessla_airpack {

static const char *const TAG = "thessla_airpack";

static const char *space_name(EntityType space) {
  switch (space) {
    case EntityType::COIL:
      return "coil";
    case EntityType::DISCRETE_INPUT:
      return "discrete";
    case EntityType::HOLDING:
      return "holding";
    case EntityType::INPUT_REGISTER:
      return "input";
    default:
      return "?";
  }
}

static uint8_t to_bcd(int value) { return static_cast<uint8_t>(((value / 10) << 4) | (value % 10)); }

// ---------------------------------------------------------------------------
// configuration

void ThesslaAirpack::request(EntityType space, uint16_t address, uint16_t count) {
  for (uint16_t i = 0; i < count; i++)
    this->wanted_.insert(key_(space, address + i));
}

void ThesslaAirpack::request_atomic(EntityType space, uint16_t address, uint16_t count) {
  for (const auto &b : this->atomic_) {
    if (b.space == space && b.start == address && b.count == count)
      return;
  }
  this->atomic_.push_back(Block{space, address, count, 0, true});
}

uint8_t ThesslaAirpack::rate_for_(EntityType space, uint16_t a) {
  switch (space) {
    case EntityType::COIL:
    case EntityType::DISCRETE_INPUT:
      return RATE_FAST;  // relays and inputs: bypass, fans, hood, humidistat
    case EntityType::INPUT_REGISTER:
      if (a >= 0x010F && a <= 0x0115)
        return RATE_FAST;  // set intensities and Constant Flow airflow
      if (a >= 0x0010 && a <= 0x0016)
        return RATE_NORMAL;  // temperatures
      return RATE_SLOW;      // firmware, serial number
    case EntityType::HOLDING:
      if (a <= 0x0003)
        return RATE_NORMAL;  // clock (minute resolution)
      if (a == 0x0500 || a == 0x0501)
        return RATE_NORMAL;  // fan control voltages
      if (a == 0x0100 || a == 0x0101 ||        // measured airflow
          a == 0x1060 || a == 0x1066 ||        // FPX
          (a >= 0x1070 && a <= 0x1075) ||      // mode, season, intensity and temperature setpoints
          a == 0x1080 ||                       // special function
          a == 0x10A0 || a == 0x10A7 ||        // GWC
          a == 0x10D0 || a == 0x10D1 ||        // EKO / KOMFORT
          a == 0x10E0 || a == 0x10EA ||        // bypass allowed / state
          a == 0x1120 || a == 0x1123 ||        // S alarm code, on/off
          a == 0x1260 ||                       // ERV heater
          a == 0x2000 || a == 0x2001)          // alarm flags
        return RATE_FAST;
      return RATE_SLOW;  // configuration, filters, dates
    default:
      return RATE_NORMAL;
  }
}

void ThesslaAirpack::build_blocks_() {
  // The controller rejects reads spanning undocumented addresses, so only strictly consecutive
  // addresses are merged, and never more than MAX_BLOCK per request.
  this->blocks_.clear();
  auto in_atomic = [this](EntityType space, uint16_t address) {
    for (const auto &b : this->atomic_) {
      if (b.space == space && address >= b.start && address < b.start + b.count)
        return true;
    }
    return false;
  };
  for (uint32_t key : this->wanted_) {  // std::set is sorted by (space, address)
    auto space = static_cast<EntityType>(key >> 16);
    auto address = static_cast<uint16_t>(key & 0xFFFF);
    if (in_atomic(space, address))
      continue;  // read by its atomic block
    const uint8_t rate = rate_for_(space, address);
    if (!this->blocks_.empty() && !this->blocks_.back().atomic) {
      Block &last = this->blocks_.back();
      if (last.space == space && last.rate == rate && last.start + last.count == address && last.count < MAX_BLOCK) {
        last.count++;
        continue;
      }
    }
    this->blocks_.push_back(Block{space, address, 1, 0, false, rate});
  }
  for (auto b : this->atomic_) {
    b.rate = rate_for_(b.space, b.start);
    this->blocks_.push_back(b);
  }
}

void ThesslaAirpack::setup() {
  // Registers the controller logic always needs, whatever entities are configured.
  this->request(EntityType::HOLDING, REG_MODE);
  this->build_blocks_();

#ifdef USE_TIME
  if (this->time_ != nullptr) {
    this->time_->add_on_time_sync_callback([this]() { this->sync_clock(); });
    // The controller's RTC drifts; correct it twice a day.
    this->set_interval("clock_sync", 12 * 60 * 60 * 1000, [this]() { this->sync_clock(); });
  }
#endif
}

void ThesslaAirpack::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Thessla Green AirPack:\n"
                "  Address: %u\n"
                "  Read requests: %u (temperatures every %u cycles, configuration every %u)",
                this->address_, static_cast<unsigned>(this->blocks_.size()), this->normal_every_,
                this->slow_every_);
  static const char *const RATE_NAMES[] = {"fast", "normal", "slow"};
  for (const auto &block : this->blocks_) {
    ESP_LOGCONFIG(TAG, "    %s 0x%04X x%u (%s)", space_name(block.space), block.start, block.count,
                  RATE_NAMES[block.rate]);
  }
  LOG_UPDATE_INTERVAL(this);
#ifdef USE_TIME
  ESP_LOGCONFIG(TAG, "  Clock sync: %s", YESNO(this->time_ != nullptr));
#endif
}

// ---------------------------------------------------------------------------
// polling

void ThesslaAirpack::update() {
  uint8_t mask = MASK_FAST;
  if (this->cycle_ % this->normal_every_ == 0)
    mask |= 1 << RATE_NORMAL;
  if (this->cycle_ % this->slow_every_ == 0)
    mask |= 1 << RATE_SLOW;
  this->cycle_++;
  this->poll_(mask);
}

void ThesslaAirpack::poll_(uint8_t mask) {
  if (this->outstanding_ > 0) {
    // A cycle is still running; read these groups as soon as it ends.
    this->pending_mask_ |= mask;
    return;
  }
  mask |= this->pending_mask_;
  this->pending_mask_ = 0;
  this->answered_in_cycle_ = 0;
  for (const auto &block : this->blocks_) {
    if (!(mask & (1 << block.rate)))
      continue;
    if (this->read_entities(block.space, block.start, block.count))
      this->outstanding_++;
  }
  if (this->outstanding_ == 0) {
    ESP_LOGW(TAG, "No read request was accepted by the modbus hub");
    return;
  }
  ESP_LOGV(TAG, "Poll started: %u requests", this->outstanding_);
}

void ThesslaAirpack::finish_request_(bool answered) {
  if (answered)
    this->answered_in_cycle_++;
  if (this->outstanding_ > 0)
    this->outstanding_--;
  if (this->outstanding_ > 0)
    return;

  // End of a poll cycle.
  ESP_LOGV(TAG, "Poll finished: %u answered", this->answered_in_cycle_);
  if (this->answered_in_cycle_ > 0) {
    if (!this->online_)
      ESP_LOGI(TAG, "Controller is answering");
    this->online_ = true;
    this->failed_cycles_ = 0;
    this->status_clear_warning();
  } else if (++this->failed_cycles_ >= 3 && this->online_) {
    ESP_LOGW(TAG, "Controller stopped answering - check RS485 wiring, address and baud rate");
    this->online_ = false;
    this->cache_.clear();
    this->status_set_warning();
  }
  this->publish_all_();
  if (this->pending_mask_)
    this->poll_(0);
}

void ThesslaAirpack::publish_all_() {
  for (auto *entity : this->entities_)
    entity->refresh();
}

void ThesslaAirpack::schedule_refresh_() {
  // The controller applies a change and recalculates intensities, setpoints and airflow over a few
  // seconds: read the fast group back right away and twice more while it settles.
  this->set_timeout("refresh1", 300, [this]() { this->poll_(MASK_FAST); });
  this->set_timeout("refresh2", 1500, [this]() { this->poll_(MASK_FAST); });
  this->set_timeout("refresh3", 4000, [this]() { this->poll_(MASK_FAST); });
}

std::optional<uint16_t> ThesslaAirpack::get(EntityType space, uint16_t address) const {
  auto it = this->cache_.find(key_(space, address));
  if (it == this->cache_.end())
    return std::nullopt;
  return it->second;
}

std::optional<uint8_t> ThesslaAirpack::operating_mode() const {
  auto mode = this->get(EntityType::HOLDING, REG_MODE);
  if (!mode.has_value() || *mode > MODE_TEMPORARY)
    return std::nullopt;
  return static_cast<uint8_t>(*mode);
}

// ---------------------------------------------------------------------------
// responses

void ThesslaAirpack::on_read_registers(EntityType entity_type, uint16_t start_address,
                                       std::span<const uint16_t> registers, modbus::ResponseStatus status) {
  if (modbus::succeeded(status)) {
    for (size_t i = 0; i < registers.size(); i++)
      this->cache_[key_(entity_type, start_address + i)] = registers[i];
    this->finish_request_(true);
    return;
  }
  this->handle_read_exception_(entity_type, start_address, static_cast<uint8_t>(*status));
  this->finish_request_(true);
}

void ThesslaAirpack::handle_read_exception_(EntityType space, uint16_t start_address, uint8_t code) {
  auto it = std::find_if(this->blocks_.begin(), this->blocks_.end(),
                         [&](const Block &b) { return b.space == space && b.start == start_address; });
  if (it == this->blocks_.end())
    return;
  const uint16_t count = it->count;
  const uint8_t rate = it->rate;
  for (uint16_t i = 0; i < count; i++)
    this->cache_.erase(key_(space, start_address + i));

  // Only "this address does not exist here" answers reshape the poll list; anything else (e.g. busy)
  // is retried as is on the next poll.
  const bool not_supported = code == 0x01 || code == 0x02 || code == 0x04;
  if (!not_supported) {
    ESP_LOGD(TAG, "Exception 0x%02X reading %s 0x%04X", code, space_name(space), start_address);
    return;
  }
  if (count > 1 && !it->atomic) {
    // One missing register (an option module this unit lacks) poisons the whole block:
    // read its members one by one from now on, so the rest keeps working.
    ESP_LOGI(TAG, "%s 0x%04X x%u rejected (0x%02X); reading these one by one", space_name(space), start_address,
             count, code);
    it = this->blocks_.erase(it);
    for (uint16_t i = 0; i < count; i++)
      it = std::next(this->blocks_.insert(it, Block{space, static_cast<uint16_t>(start_address + i), 1, 0, false, rate}));
    this->pending_mask_ |= 1 << rate;
    return;
  }
  if (++it->failures >= 3) {
    ESP_LOGI(TAG, "%s 0x%04X is not supported by this unit; no longer polled", space_name(space), start_address);
    this->blocks_.erase(it);
  }
}

void ThesslaAirpack::on_read_bits(EntityType entity_type, uint16_t start_address, modbus::PackedBits bits,
                                  modbus::ResponseStatus status) {
  if (modbus::succeeded(status)) {
    for (uint16_t i = 0; i < bits.size(); i++)
      this->cache_[key_(entity_type, start_address + i)] = bits[i] ? 1 : 0;
  } else {
    this->handle_read_exception_(entity_type, start_address, static_cast<uint8_t>(*status));
  }
  this->finish_request_(true);
}

bool ThesslaAirpack::on_no_response(std::span<const uint8_t> request_pdu) {
  // A read for a write that timed out: count it so the cycle can end. No hub retry - the next poll
  // repeats the read anyway, and a write is better reported than blindly repeated.
  if (!request_pdu.empty() && modbus::helpers::is_function_code_write(request_pdu[0])) {
    ESP_LOGW(TAG, "Write got no response from the controller");
    return false;
  }
  this->finish_request_(false);
  return false;
}

void ThesslaAirpack::on_not_sent(std::span<const uint8_t> request_pdu) {
  if (!request_pdu.empty() && modbus::helpers::is_function_code_write(request_pdu[0]))
    return;
  this->finish_request_(false);
}

void ThesslaAirpack::on_write_single_register(uint16_t address, uint16_t value, modbus::ResponseStatus status) {
  if (!modbus::succeeded(status)) {
    ESP_LOGW(TAG, "Controller rejected value %u for register 0x%04X (exception 0x%02X)", value, address,
             static_cast<uint8_t>(*status));
    this->publish_all_();  // put the UI back to the real value
    return;
  }
  ESP_LOGD(TAG, "Wrote 0x%04X = %u", address, value);
  // A command register reads back something else than was written: wait for the refresh.
  if (address != REG_FILTER_COMMAND)
    this->store_(address, value);
  this->publish_all_();
  this->schedule_refresh_();
}

void ThesslaAirpack::on_write_multiple_registers(uint16_t start_address, std::span<const uint16_t> registers,
                                                 modbus::ResponseStatus status) {
  if (!modbus::succeeded(status)) {
    ESP_LOGW(TAG, "Controller rejected block write at 0x%04X (exception 0x%02X)", start_address,
             static_cast<uint8_t>(*status));
    this->publish_all_();
    return;
  }
  ESP_LOGD(TAG, "Wrote %u registers at 0x%04X", static_cast<unsigned>(registers.size()), start_address);
  // Mirror the temporary-mode command blocks onto the registers they change, so the UI follows at once.
  if (start_address == REG_TEMP_INTENSITY_BLOCK && registers.size() == 3) {
    this->store_(REG_MODE, MODE_TEMPORARY);
    this->store_(REG_TEMP_INTENSITY, registers[1]);
  } else if (start_address == REG_TEMP_TEMPERATURE_BLOCK && registers.size() == 3) {
    this->store_(REG_MODE, MODE_TEMPORARY);
    this->store_(REG_TEMP_TEMPERATURE, registers[1]);
  }
  this->publish_all_();
  this->schedule_refresh_();
}

// ---------------------------------------------------------------------------
// writes

void ThesslaAirpack::write_register(uint16_t address, uint16_t value) {
  if (!this->write_single_register(address, value))
    ESP_LOGW(TAG, "Write of 0x%04X was refused by the modbus hub", address);
}

void ThesslaAirpack::write_registers(uint16_t address, std::span<const uint16_t> values) {
  if (!this->write_multiple_registers(address, values))
    ESP_LOGW(TAG, "Block write at 0x%04X was refused by the modbus hub", address);
}

void ThesslaAirpack::start_temporary_intensity(uint8_t percent) {
  percent = std::clamp<uint8_t>(percent, 10, 100);
  const uint16_t block[3] = {MODE_TEMPORARY, percent, 1};
  this->write_registers(REG_TEMP_INTENSITY_BLOCK, block);
}

void ThesslaAirpack::set_intensity(uint8_t percent) {
  percent = std::clamp<uint8_t>(percent, 10, 100);
  auto mode = this->operating_mode();
  if (!mode.has_value()) {
    // Which register to write depends on the mode; never guess.
    ESP_LOGW(TAG, "Operating mode not read yet - intensity change ignored");
    this->publish_all_();
    return;
  }
  if (*mode == MODE_MANUAL) {
    this->write_register(REG_MANUAL_INTENSITY, percent);
  } else {
    this->start_temporary_intensity(percent);
  }
}

void ThesslaAirpack::set_supply_temperature(float celsius) {
  celsius = std::clamp(celsius, 10.0f, 45.0f);
  auto raw = static_cast<uint16_t>(lroundf(celsius * 2.0f));
  auto mode = this->operating_mode();
  if (!mode.has_value()) {
    ESP_LOGW(TAG, "Operating mode not read yet - temperature change ignored");
    this->publish_all_();
    return;
  }
  if (*mode == MODE_MANUAL) {
    this->write_register(REG_MANUAL_TEMPERATURE, raw);
  } else {
    const uint16_t block[3] = {MODE_TEMPORARY, raw, 1};
    this->write_registers(REG_TEMP_TEMPERATURE_BLOCK, block);
  }
}

void ThesslaAirpack::sync_clock() {
#ifdef USE_TIME
  if (this->time_ == nullptr) {
    ESP_LOGW(TAG, "Clock sync needs time_id");
    return;
  }
  ESPTime now = this->time_->now();
  if (!now.is_valid()) {
    ESP_LOGD(TAG, "Local time not valid yet, clock sync postponed");
    return;
  }
  // ESPTime: 1 = Sunday ... 7 = Saturday; controller: 0 = Monday ... 6 = Sunday.
  const uint8_t weekday = static_cast<uint8_t>((now.day_of_week + 5) % 7);
  const uint16_t regs[4] = {
      static_cast<uint16_t>((to_bcd(now.year % 100) << 8) | to_bcd(now.month)),
      static_cast<uint16_t>((to_bcd(now.day_of_month) << 8) | weekday),
      static_cast<uint16_t>((to_bcd(now.hour) << 8) | to_bcd(now.minute)),
      static_cast<uint16_t>(to_bcd(now.second) << 8),
  };
  ESP_LOGI(TAG, "Setting controller clock to %04u-%02u-%02u %02u:%02u", now.year, now.month, now.day_of_month,
           now.hour, now.minute);
  this->write_registers(REG_CLOCK, regs);
#else
  ESP_LOGW(TAG, "Clock sync needs a time component");
#endif
}

}  // namespace esphome::thessla_airpack
