# Mapa rejestrów Modbus – Thessla Green AirPack 4

Źródło: **Protokół Modbus RTU – Tabela rejestrów MODBUS_USER_AirPack_4_08.2022.01** (Thessla Green).
Wszystkie adresy poniżej zostały sprawdzone z tym dokumentem. Adresy są zero-based, tak jak na kablu.

- Fabrycznie: **9600 bps 8N1, adres 10**. Port AirB ma własne nastawy (0x1164–0x1167, od fw 4.76).
- Odczyt i zapis maksymalnie **16 rejestrów** naraz. Odczyt przez nieistniejące adresy kończy się
  wyjątkiem, dlatego komponent łączy w zapytanie tylko rejestry sąsiadujące, a odrzucony blok
  rozbija na pojedyncze odczyty.
- „fw” = minimalna wersja oprogramowania sterownika podana w dokumencie.

## Podłączenie (gniazdo 10 „AirMobile / ModbusRTU”, RJ45)

| Żyła (T568B) | Pin | Sygnał |
|---|---|---|
| biało-pomarańczowa | 1 | RS485 **B−** |
| pomarańczowa | 2 | RS485 **A+** |
| biało-brązowa | 7 | **+24 V DC** |
| brązowa | 8 | **GND** |

Nazwa i numer gniazda pochodzą z instrukcji AirPack4 h. Dokumenty producenta nie podają pinoutu:
A/B/GND pochodzą z projektu eltrue-thessla-green-airpack-ha, a +24 V z raportu użytkownika
(ThesslaGreen_HA, issue #34). Zmierz napięcie przed podłączeniem. W tabeli rejestrów i innych
projektach spotkasz nazwę port AirB.

## 01 Coils (odczyt)

| Adres | Nazwa | Znaczenie |
|---|---|---|
| 5 | duct_water_heater_pump | pompa obiegowa nagrzewnicy |
| 9 | bypass | siłownik przepustnicy bypass |
| 10 | info | potwierdzenie pracy (O1), fw 4.84 |
| 11 | power_supply_fans | zasilanie wentylatorów |
| 12 | heating_cable | kabel grzejny |
| 13 | work_permit | potwierdzenie pracy (Expansion) |
| 14 | gwc | przekaźnik GWC |
| 15 | hood | przepustnica okapu |

## 02 Discrete inputs

| Adres | Nazwa | Znaczenie |
|---|---|---|
| 0 | duct_heater_protection | zabezpieczenie termiczne nagrzewnicy kanałowej |
| 1 | expansion | komunikacja z modułem Expansion |
| 3 | dp_duct_filter_overflow | presostat filtra kanałowego |
| 4 | hood | włącznik OKAP |
| 5 | contamination_sensor | czujnik jakości powietrza |
| 6 | airing_sensor | higrostat |
| 7 | airing_switch | włącznik WIETRZENIE |
| 10–13 | airing_mini, fan_speed_3/2/1 | pozycje przełącznika AirS |
| 14 | fireplace | włącznik KOMINEK |
| 15 | ppoz | alarm pożarowy |
| 18 | dp_ahu_filter_overflow | presostat filtrów (DP1) |
| 19 | ahu_filter_protection | zabezpieczenie termiczne nagrzewnicy FPX |
| 21 | empty_house | sygnał PUSTY DOM |

## 04 Input registers

| Adres | Hex | Nazwa | Kodowanie |
|---|---|---|---|
| 0, 1, 4 | 0x0000… | wersja firmware MM.mm.pp | uint |
| 2 / 3 | 0x0002 / 3 | bieżący dzień / odcinek harmonogramu | 0 = pon. / 0–3 |
| 16 | 0x0010 | TZ1 – zewnętrzna (czerpnia) | int16 × 0,1 °C, 0x8000 = brak |
| 17 | 0x0011 | TN1 – nawiew | j.w. |
| 18 | 0x0012 | TP – wywiew z pomieszczeń | j.w. |
| 19 | 0x0013 | TZ2 – za nagrzewnicą FPX | j.w. |
| 20 | 0x0014 | TN2 – za nagrzewnicą/chłodnicą kanałową | j.w. |
| 21 | 0x0015 | TZ3 – przed wymiennikiem glikolowym GWC | j.w. |
| 22 | 0x0016 | TO – otoczenie centrali | j.w. |
| 24–29 | 0x0018 | numer seryjny sterownika | 1 bajt na rejestr |
| 271 | 0x010F | Constant Flow aktywny | 0/1 |
| 272 / 273 | 0x0110 / 1 | zadana intensywność nawiewu / wywiewu | % (0–150) |
| 274 / 275 | 0x0112 / 3 | zadany strumień nawiewu / wywiewu | m³/h |
| 276 / 277 | 0x0114 / 5 | min. / maks. intensywność | % |

## 03 Holding registers

| Adres | Hex | Nazwa | Kodowanie | Zapis |
|---|---|---|---|---|
| 0–3 | 0x0000 | zegar [RRMM][DDTT][GGmm][sscc] | BCD, TT: 0 = pon. | 4 rejestry naraz |
| 16–127 | 0x0010 | harmonogramy lato/zima: godziny i [AATT] | BCD / % + T×2 | ✔ |
| 128–180 | 0x0080 | godziny wietrzenia w harmonogramie | BCD, 0x2400 = wył. | ✔ |
| 256 / 257 | 0x0100 / 1 | chwilowy strumień nawiewu / wywiewu | m³/h, 65535 = CF nieaktywny | – |
| 1280 / 1281 | 0x0500 / 1 | napięcie sterujące wentylatorem nawiewu / wywiewu (0–10 V) | × 0,00244 V | – |
| 4192 | 0x1060 | system FPX uruchomiony | 0/1 | – |
| 4198 | 0x1066 | tryb FPX | 0 off, 1 FPX1, 2 FPX2 | – |
| 4208 | 0x1070 | tryb pracy | 0 auto, 1 manual, 2 chwilowy | ✔ |
| 4209 | 0x1071 | harmonogram | 0 lato, 1 zima | ✔ |
| 4210 | 0x1072 | intensywność – MANUAL | 10–100 % | ✔ |
| 4211 | 0x1073 | intensywność – CHWILOWY | 10–100 % | blokiem 0x1130 |
| 4212 | 0x1074 | temp. nawiewu – MANUAL (KOMFORT) | × 0,5 °C | ✔ |
| 4213 | 0x1075 | temp. nawiewu – CHWILOWY | × 0,5 °C | blokiem 0x1133 |
| 4224 | 0x1080 | funkcja specjalna | 0–11, niżej | ✔ |
| 4226–4239 | 0x1082… | intensywności i czasy funkcji (okap, kominek, wietrzenie, pusty dom, otwarte okna) | %, min | ✔ |
| 4256 | 0x10A0 | GWC wyłączony | 0 = aktywny | ✔ |
| 4263 | 0x10A7 | status GWC | 0 nieakt., 1 zima, 2 lato | – |
| 4304 | 0x10D0 | EKO / KOMFORT | 0 / 1 | ✔ |
| 4305 | 0x10D1 | status KOMFORT | 0, 1 grzanie, 2 chłodzenie | – |
| 4320 | 0x10E0 | bypass wyłączony | 0 = aktywny | ✔ |
| 4321 | 0x10E1 | min. TZ1 dla bypass | × 0,5 °C (5–20 °C) | ✔ |
| 4322 / 4323 | 0x10E2 / 3 | TP aktywacji bypass: grzanie / chłodzenie | × 0,5 °C (15–30 °C) | ✔ |
| 4330 | 0x10EA | status bypass | 0, 1 freeheating, 2 freecooling | – |
| 4331 | 0x10EB | tryb bypass | 1–3 | ✔ |
| 4354 / 4355 | 0x1102 / 3 | nominalny strumień nawiewu / wywiewu (100 %) | m³/h | serwis |
| 4384 | 0x1120 | **numer alarmu S, który zatrzymał centralę** | 0 = brak | – |
| 4387 | 0x1123 | centrala ON/OFF | 0/1 | ✔ |
| 4399 | 0x112F | język panelu Air++ | 0 PL … 4 SK | ✔ |
| 4400–4402 | 0x1130 | **blok: chwilowa intensywność** `[2, %, 1]` | – | 3 rejestry naraz |
| 4403–4405 | 0x1133 | **blok: chwilowa temperatura** `[2, T×2, 1]` | – | 3 rejestry naraz |
| 4452–4455 | 0x1164 | port AirB: adres, prędkość, parzystość, stop | fw 4.76 | ✔ |
| 4482 | 0x1182 | **zużycie filtra nawiewnego** | 0–127 % (ekran główny), fw 4.75 | – |
| 4483 | 0x1183 | **zużycie filtra wywiewnego** | j.w. | – |
| 4660 | 0x1234 | **data wymiany filtra nawiewnego** | dzień b0–4, miesiąc b5–8, rok−2000 b9–15 | – |
| 4662 | 0x1236 | **data wymiany filtra wywiewnego** | j.w. | – |
| 4704 | 0x1260 | nagrzewnica wtórna ERV aktywna | 0/1, fw 4.85 | – |
| 4711 | 0x1267 | tryb ERV | 0 wył., 1, 2 | ✔ |
| 8144–8151 | 0x1FD0 | nazwa urządzenia | ASCII, 2 znaki na rejestr | ✔ |
| 8190 | 0x1FFE | temperatura zadana KOMFORT | × 0,5 °C | – |
| 8191 | 0x1FFF | odczyt: typ filtrów (AirPack4: 4 = CleanPad Pure) | – | **polecenie:** 0x11 nawiewny, 0x21 wywiewny wymieniony |
| 8192 | 0x2000 | jest ostrzeżenie E | 0/1 | – |
| 8193 | 0x2001 | jest alarm S | 0/1 | – |
| 8194–8224 | 0x2002… | alarmy S2–S32 (z lukami) | 0/1 | kasowanie |
| 8291–8443 | 0x2063… | ostrzeżenia E99–E251 i F142–F147 (z lukami) | 0/1 | kasowanie |

Funkcje specjalne (0x1080): 0 brak, 1 OKAP, 2 KOMINEK, 3 wietrzenie (dzwonkowy), 4 wietrzenie (ON/OFF),
5 wietrzenie (higrostat), 6 wietrzenie (czujnik JP), 7 **wietrzenie ręczne**, 8 wietrzenie (harmonogram
AUTO), 9 wietrzenie (harmonogram MANUAL), 10 **otwarte okna**, 11 **pusty dom**.
Ręcznie (z panelu, karty lub HA) można ustawić: 0, 2, 7, 10, 11. Okap działa tylko z wejścia sygnałowego.

## Zapisy złożone

- **Tryb chwilowy** – jednym zapisem FC16 trzech rejestrów: `0x1130 ← [2, %, 1]` (intensywność) lub
  `0x1133 ← [2, T×2, 1]` (temperatura nawiewu). Komponent robi to sam: w AUTO zmiana intensywności
  włącza tryb chwilowy, w MANUAL zmienia nastawę 0x1072.
- **Zegar** – cztery rejestry 0x0000–0x0003 zawsze razem.
- **Wymiana filtrów** (AirPack4 bez AFC) – zapis 0x11 lub 0x21 do 0x1FFF rejestruje wymianę,
  kasuje alarm i wyznacza nowy termin. Dwa filtry to dwa osobne polecenia.
