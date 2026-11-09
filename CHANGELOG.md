# Changelog

Wszystkie istotne zmiany w komponencie. Format: [Keep a Changelog](https://keepachangelog.com/pl/1.1.0/),
wersje: [Semantic Versioning](https://semver.org/lang/pl/). Opis wydania na GitHubie powstaje z sekcji
danej wersji, więc każde wydanie musi mieć tu swój wpis.

## [Unreleased]

## [1.2.0] - 2026-11-09

### Dodane

- Harmonogram tygodniowy trybu AUTO (lato i zima, 4 odcinki na dzień, godzina wietrzenia):
  - `text_sensor` `schedule_summer` / `schedule_winter` – rejestry sezonu w hex, odczyt co `slow_interval`,
  - `text` `schedule_command` – zapis jednego lub kilku dni jednym poleceniem, z kontrolą zakresów
    z tabeli producenta przed wysłaniem; stan encji pokazuje wynik (`OK: …` / `Błąd: …`).
- Edycja harmonogramu w karcie thessla-airpack-card 2.4.0.
- Symulator: fabryczny harmonogram z instrukcji i walidacja zapisów; test CI zapisu harmonogramu.

## [1.1.0] - 2026-11-09

### Dodane

- Sensory `supply_fan_voltage` i `extract_fan_voltage`: napięcie sterujące wentylatorami 0–10 V
  (rejestry 0x0500/0x0501), np. do wykrywania zamarzniętej czerpni przy włączonym Constant Flow.

## [1.0.0] - 2026-10-30

### Dodane

- Pierwsze wydanie: odczyt i sterowanie Thessla Green AirPack 4 przez Modbus RTU – temperatury, przepływy,
  tryby pracy, funkcje specjalne, bypass, GWC, filtry, alarmy, zegar centrali.

[Unreleased]: https://github.com/tomaszsluszniak/esphome-thessla-airpack/compare/v1.2.0...HEAD
[1.2.0]: https://github.com/tomaszsluszniak/esphome-thessla-airpack/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/tomaszsluszniak/esphome-thessla-airpack/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/tomaszsluszniak/esphome-thessla-airpack/releases/tag/v1.0.0
