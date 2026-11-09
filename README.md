# esphome-thessla-airpack

[![CI](https://github.com/tomaszsluszniak/esphome-thessla-airpack/actions/workflows/ci.yml/badge.svg)](https://github.com/tomaszsluszniak/esphome-thessla-airpack/actions/workflows/ci.yml)
[![Wydanie](https://img.shields.io/github/v/release/tomaszsluszniak/esphome-thessla-airpack)](https://github.com/tomaszsluszniak/esphome-thessla-airpack/releases/latest)
[![Licencja: MIT](https://img.shields.io/github/license/tomaszsluszniak/esphome-thessla-airpack)](LICENSE)

Komponent [ESPHome](https://esphome.io) do odczytu i sterowania rekuperatorem **Thessla Green AirPack**
przez Modbus RTU (RS485, gniazdo AirMobile / ModbusRTU). Bramka na ESP32 wystawia do Home Assistant temperatury, przepływy,
tryby pracy, funkcje specjalne, stan filtrów i alarmy, bez chmury i bez modułu producenta.

*ESPHome external component for Thessla Green AirPack heat recovery units (Modbus RTU over RS485).
The documentation is in Polish.*

Do wyświetlania w Home Assistant pasuje karta
[thessla-airpack-card](https://github.com/tomaszsluszniak/thessla-airpack-card).

> [!NOTE]
> Projekt niezależny, niezwiązany z firmą Thessla Green. Komponent zapisuje ustawienia centrali
> przez Modbus; używasz go na własną odpowiedzialność.

## Zgodność

Mapa rejestrów pochodzi z tabeli producenta *MODBUS_USER_AirPack_4_08.2022.01* i dotyczy **AirPack 4**;
szczegóły w [docs/rejestry.md](docs/rejestry.md). Działanie z innymi generacjami nie jest gwarantowane.
Część funkcji wymaga nowszego firmware sterownika (np. zużycie filtrów od 4.75, ERV od 4.85).

## Czego potrzebujesz

- ESP32 z ESPHome **2026.8** lub nowszym,
- transceiver RS485 zasilany z **3,3 V**, np. MAX3485 (wejścia ESP32 nie tolerują 5 V),
- kabel do gniazda **10 AirMobile / ModbusRTU** (patrz [Podłączenie](#podłączenie)),
- zasilanie bramki: przetwornica z 24 V z tego gniazda albo osobny zasilacz.

## Instalacja

Dodaj do manifestu urządzenia ESPHome:

```yaml
external_components:
  - source: github://tomaszsluszniak/esphome-thessla-airpack@v1.1.0
    components: [thessla_airpack]
```

Pełny przykład z wszystkimi encjami: [example/airpack.yaml](example/airpack.yaml).

## Minimalna konfiguracja

Fragment manifestu; piny dopasuj do swojej bramki.

```yaml
uart:
  id: rs485
  tx_pin: GPIO17
  rx_pin: GPIO16
  baud_rate: 9600          # fabrycznie 9600 8N1
  parity: NONE

modbus:
  id: modbus_bus
  uart_id: rs485
  flow_control_pin: GPIO4  # DE/RE transceivera; usuń, jeśli moduł RS485 przełącza kierunek sam
  send_wait_time: 300ms
  turnaround_time: 20ms    # ważne: ESPHome domyślnie czeka 600 ms przed każdym zapytaniem

time:
  - platform: homeassistant
    id: ha_time

thessla_airpack:
  id: airpack
  modbus_id: modbus_bus
  address: 10              # fabrycznie 10
  time_id: ha_time         # opcjonalnie: synchronizacja zegara centrali

sensor:
  - platform: thessla_airpack
    outside_temperature:
      name: Temperatura zewnętrzna (TZ1)
    intensity:
      name: Intensywność wentylacji

select:
  - platform: thessla_airpack
    operating_mode:
      name: Tryb pracy
```

## Odświeżanie

| Opcja | Domyślnie | Co odczytuje |
|---|---|---|
| `update_interval` | `2s` | tryb, sezon, intensywności, przepływy, funkcje, bypass, alarmy, wejścia i przekaźniki |
| `normal_interval` | `10s` | temperatury, zegar |
| `slow_interval` | `60s` | filtry, daty wymiany, nastawy bypass, firmware, numer seryjny |

Po każdej zmianie wykonanej z Home Assistant komponent od razu odczytuje szybką grupę jeszcze trzy razy
(po ok. 0,3 s, 1,5 s i 4 s), bo centrala przelicza intensywność i przepływy przez kilka sekund.
Odpytywane są tylko rejestry skonfigurowanych encji; sąsiednie rejestry łączone są w jedno zapytanie
(maks. 16, wymóg producenta). Gdy centrala odrzuci blok (brak modułu, np. TN2 lub GWC), komponent sam
przechodzi na pojedyncze odczyty.

Przy kompilacji pojawi się ostrzeżenie, jeśli `turnaround_time` hubu modbus przekracza 100 ms.

## Encje

| Platforma | Klucze |
|---|---|
| `sensor` | `outside_temperature` (TZ1), `supply_temperature` (TN1), `extract_temperature` (TP), `fpx_temperature` (TZ2), `duct_temperature` (TN2), `gwc_temperature` (TZ3), `ambient_temperature` (TO), `supply_flow`, `extract_flow`, `supply_target_flow`, `extract_target_flow`, `supply_percentage`, `extract_percentage`, `supply_fan_voltage`, `extract_fan_voltage`, `intensity`, `filter_supply_wear`, `filter_extract_wear`, `alarm_s_code` |
| `binary_sensor` | `online`, `alarm_s`, `alarm_e`, `bypass_open`, `fans_powered`, `heating_cable`, `gwc_active`, `hood_damper`, `fpx_active`, `erv_active`, `constant_flow_active`, `hood_input`, `air_quality_input`, `humidistat_input`, `fireplace_input`, `fire_alarm` |
| `text_sensor` | `special_function`, `bypass_status`, `comfort_status`, `gwc_status`, `fpx_stage`, `filter_type`, `filter_supply_date`, `filter_extract_date`, `firmware_version`, `serial_number`, `controller_clock` |
| `select` | `operating_mode`, `season`, `special_function`, `comfort_mode`, `bypass_mode`, `erv_mode` |
| `number` | `intensity`, `manual_intensity`, `supply_temperature_setpoint`, `bypass_min_temperature`, `bypass_heating_temperature`, `bypass_cooling_temperature` |
| `switch` | `power`, `bypass_enabled`, `gwc_enabled` |
| `button` | `sync_clock`, `filter_supply_replaced`, `filter_extract_replaced` |

Zachowanie wymagające uwagi:

- `number.intensity` działa jak panel: w trybie MANUAL zmienia nastawę manualną, w AUTO uruchamia tryb
  chwilowy (atomowy zapis trzech rejestrów od 0x1130). `supply_temperature_setpoint` analogicznie (0x1133).
- `select.special_function` pozwala ustawić tylko funkcje, które producent dopuszcza ręcznie: brak, kominek,
  wietrzenie, otwarte okna, pusty dom. Okap i wietrzenie z higrostatu uruchamia wyłącznie wejście centrali.
- `button.filter_*_replaced` zapisuje wymianę filtra (polecenia 0x11 / 0x21 do 0x1FFF): kasuje alarm
  i wyznacza nowy termin. Używaj dopiero po faktycznej wymianie.
- Zegar centrali (0x0000–0x0003) jest zawsze odczytywany i zapisywany jako jeden blok czterech rejestrów.

## Podłączenie

Bramkę podłącz do gniazda **10 „AirMobile / ModbusRTU”** (RJ45) w porcie przyłączeniowym centrali, obok
króćca wyrzutni. Numeracja pochodzi z instrukcji AirPack4 h; w innych wykonaniach sprawdź opis portu.
Jeśli masz moduł AirMobile, to gniazdo jest przez niego zajęte.

Wtyk RJ45 w standardzie T568B:

| Pin RJ45 | Żyła | Sygnał |
|---|---|---|
| 1 | biało-pomarańczowa | RS485 **B−** |
| 2 | pomarańczowa | RS485 **A+** |
| 7 | biało-brązowa | **+24 V DC** |
| 8 | brązowa | **GND** |

> [!WARNING]
> Producent nie publikuje tego pinoutu ani dopuszczalnego prądu. Pochodzi on od użytkowników
> ([A/B/GND](https://github.com/staszekj/eltrue-thessla-green-airpack-ha),
> [+24 V](https://github.com/aLAN-LDZ/ThesslaGreen_HA/issues/34)), więc przed podłączeniem bramki
> zmierz napięcie między pinami 7 i 8. Nie podłączaj 24 V bezpośrednio do ESP32 ani transceivera: użyj
> przetwornicy na 3,3 V lub 5 V. Pozostałe piny zostaw niepodłączone, a kabel wpinaj przy odłączonej
> centrali. Instrukcja producenta ostrzega, że błędne połączenie może uszkodzić moduł BASIC.

Przy krótkim przewodzie i 9600 bd terminacja 120 Ω jest zbędna.

## Testy bez centrali

`tools/airpack_simulator.py` udaje centralę na wirtualnym porcie szeregowym: odpowiada jak sterownik,
odrzuca nieistniejące adresy i niepełny odczyt zegara, a po zmianie trybu przelicza intensywność
z opóźnieniem i stopniowo zmienia przepływ. `tests/host_e2e.yaml` buduje cały firmware na Linuksa
(platforma ESPHome `host`) i wykonuje na symulatorze wszystkie rodzaje zapisów. Oba testy uruchamia
GitHub Actions przy każdym commicie.

## Zgłoszenia

Błędy i pomysły zgłaszaj w [Issues](https://github.com/tomaszsluszniak/esphome-thessla-airpack/issues).
Przy problemach z komunikacją dołącz model centrali, wersję firmware i log ESPHome.

## Licencja

[MIT](LICENSE)
