#pragma once

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/components/modbus/modbus.h"

#ifdef USE_TIME
#include "esphome/components/time/real_time_clock.h"
#endif

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <vector>

namespace esphome::thessla_airpack {

using modbus::EntityType;

// Wire addresses used by the controller's own logic (see docs/rejestry.md).
static constexpr uint16_t REG_CLOCK = 0x0000;
static constexpr uint16_t REG_SUPPLY_PERCENT = 0x0110;   // input
static constexpr uint16_t REG_MODE = 0x1070;             // holding: 0 auto, 1 manual, 2 temporary
static constexpr uint16_t REG_MANUAL_INTENSITY = 0x1072;
static constexpr uint16_t REG_TEMP_INTENSITY = 0x1073;
static constexpr uint16_t REG_MANUAL_TEMPERATURE = 0x1074;
static constexpr uint16_t REG_TEMP_TEMPERATURE = 0x1075;
static constexpr uint16_t REG_TEMP_INTENSITY_BLOCK = 0x1130;    // [2, %, 1]
static constexpr uint16_t REG_TEMP_TEMPERATURE_BLOCK = 0x1133;  // [2, T*2, 1]
static constexpr uint16_t REG_FILTER_COMMAND = 0x1FFF;          // read: filter type, write: 0x11/0x21 command

// Weekly schedules of AUTO mode: season (0 summer, 1 winter) x day (0 = Monday) x 4 time slots.
static constexpr uint16_t REG_SCHEDULE_START = 0x0010;    // [GGMM] BCD start of each slot
static constexpr uint16_t REG_SCHEDULE_SETTING = 0x0048;  // [AATT] intensity % and supply temperature x2
static constexpr uint16_t REG_SCHEDULE_AIRING = 0x0080;   // [GGMM] BCD airing start, one per day (stride 4)
static constexpr uint16_t REG_SCHEDULE_END = 0x00B4;
static constexpr uint16_t SCHEDULE_SEASON_STRIDE = 28;
static constexpr uint16_t SCHEDULE_SLOT_OFF = 0xA200;
static constexpr uint16_t SCHEDULE_AIRING_OFF = 0x2400;
static constexpr uint16_t SCHEDULE_DEFAULT_SETTING = (30 << 8) | 44;  // 30 %, 22 °C

inline uint16_t schedule_start_reg(uint8_t season, uint8_t day, uint8_t slot) {
  return REG_SCHEDULE_START + season * SCHEDULE_SEASON_STRIDE + day * 4 + slot;
}
inline uint16_t schedule_setting_reg(uint8_t season, uint8_t day, uint8_t slot) {
  return REG_SCHEDULE_SETTING + season * SCHEDULE_SEASON_STRIDE + day * 4 + slot;
}
inline uint16_t schedule_airing_reg(uint8_t season, uint8_t day) {
  return REG_SCHEDULE_AIRING + season * SCHEDULE_SEASON_STRIDE + day * 4;
}

static constexpr uint8_t MODE_AUTO = 0;
static constexpr uint8_t MODE_MANUAL = 1;
static constexpr uint8_t MODE_TEMPORARY = 2;

// The controller answers at most 16 registers (or bits) per request.
static constexpr uint16_t MAX_BLOCK = 16;

// Poll groups: what the user changes or watches react every update_interval, temperatures less
// often, configuration and identity rarely.
static constexpr uint8_t RATE_FAST = 0;
static constexpr uint8_t RATE_NORMAL = 1;
static constexpr uint8_t RATE_SLOW = 2;
static constexpr uint8_t MASK_FAST = 1 << RATE_FAST;
static constexpr uint8_t MASK_ALL = 0x07;

class ThesslaAirpack;

/// Every entity of this component refreshes itself from the parent's register cache.
class ThesslaEntity {
 public:
  virtual ~ThesslaEntity() = default;
  virtual void refresh() = 0;
};

class ThesslaAirpack : public PollingComponent, public modbus::ModbusClientDevice {
 public:
  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // ---- configuration (codegen) ----
  /// Ask the poller to read `count` consecutive registers/bits starting at `address`.
  void request(EntityType space, uint16_t address, uint16_t count = 1);
  /// Registers the controller only answers as one whole read (e.g. the clock): never merged or split.
  void request_atomic(EntityType space, uint16_t address, uint16_t count);
  void add_entity(ThesslaEntity *entity) { this->entities_.push_back(entity); }
  /// How many update_interval cycles between temperature reads / configuration reads.
  void set_normal_every(uint16_t cycles) { this->normal_every_ = cycles ? cycles : 1; }
  void set_slow_every(uint16_t cycles) { this->slow_every_ = cycles ? cycles : 1; }
#ifdef USE_TIME
  void set_time(time::RealTimeClock *time) { this->time_ = time; }
#endif

  // ---- cache access for entities ----
  std::optional<uint16_t> get(EntityType space, uint16_t address) const;
  std::optional<int16_t> get_signed(EntityType space, uint16_t address) const {
    auto v = this->get(space, address);
    if (!v.has_value())
      return std::nullopt;
    return static_cast<int16_t>(*v);
  }
  bool is_online() const { return this->online_; }
  std::optional<uint8_t> operating_mode() const;

  // ---- write API ----
  void write_register(uint16_t address, uint16_t value);
  void write_registers(uint16_t address, std::span<const uint16_t> values);
  /// Behaves like the panel: in MANUAL the manual setpoint changes, otherwise temporary mode is started.
  void set_intensity(uint8_t percent);
  void set_supply_temperature(float celsius);
  /// Explicitly start temporary mode with the given intensity.
  void start_temporary_intensity(uint8_t percent);
  void sync_clock();
  /// Writes one day's schedule to one or more days of a season. Command (space-separated):
  ///   <L|Z><days 1-7> <slot1> <slot2> <slot3> <slot4> <airing>
  /// slot: "HH:MM/percent" or "HH:MM/percent/temperature", "-" = slot off; airing: "HH:MM" or "-".
  /// A slot without temperature keeps the one stored for it. Returns an empty string when the writes
  /// were queued, otherwise why the command was refused (nothing is written then).
  std::string set_schedule(const std::string &command);

  // ---- ModbusClientDevice ----
  void on_read_registers(EntityType entity_type, uint16_t start_address, std::span<const uint16_t> registers,
                         modbus::ResponseStatus status) override;
  void on_read_bits(EntityType entity_type, uint16_t start_address, modbus::PackedBits bits,
                    modbus::ResponseStatus status) override;
  void on_write_single_register(uint16_t address, uint16_t value, modbus::ResponseStatus status) override;
  void on_write_multiple_registers(uint16_t start_address, std::span<const uint16_t> registers,
                                   modbus::ResponseStatus status) override;
  bool on_no_response(std::span<const uint8_t> request_pdu) override;
  void on_not_sent(std::span<const uint8_t> request_pdu) override;

 protected:
  struct Block {
    EntityType space;
    uint16_t start;
    uint16_t count;
    uint8_t failures{0};
    bool atomic{false};
    uint8_t rate{RATE_NORMAL};
  };
  static uint8_t rate_for_(EntityType space, uint16_t address);
  void handle_read_exception_(EntityType space, uint16_t start_address, uint8_t code);

  static uint32_t key_(EntityType space, uint16_t address) {
    return (static_cast<uint32_t>(space) << 16) | address;
  }
  void build_blocks_();
  void poll_(uint8_t mask);
  void finish_request_(bool answered);
  void publish_all_();
  void schedule_refresh_();
  void store_(uint16_t address, uint16_t value) { this->cache_[key_(EntityType::HOLDING, address)] = value; }

  std::set<uint32_t> wanted_;
  std::vector<Block> atomic_;
  std::vector<Block> blocks_;
  std::map<uint32_t, uint16_t> cache_;
  std::vector<ThesslaEntity *> entities_;

  uint16_t outstanding_{0};
  uint16_t answered_in_cycle_{0};
  uint8_t failed_cycles_{0};
  bool online_{false};
  uint8_t pending_mask_{0};
  uint32_t cycle_{0};
  uint16_t normal_every_{5};
  uint16_t slow_every_{30};

#ifdef USE_TIME
  time::RealTimeClock *time_{nullptr};
#endif
};

}  // namespace esphome::thessla_airpack
