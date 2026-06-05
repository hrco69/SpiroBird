# SpiroBird — Ožičenje (wiring)

> Sve pinove je moguće promijeniti u `controller/include/config.h`.
> Vrijednosti u ovom dokumentu su **zadane pretpostavke** — ako se na konkretnoj ploči
> pokažu problematičnima, promijeniti u `config.h` i ažurirati ovaj dokument.

## Pregled — Controller (Master) ESP32-S3-N16R8

| Signal | GPIO | Smjer | Komponenta | Napomena |
|---|---|---|---|---|
| `PIN_POT_ADC` | **4** | ulaz (ADC) | potenciometar B10K, wiper | ADC1, 12-bit 0–4095 |
| `PIN_BUZZER` | **15** | izlaz (LEDC PWM) | pasivni buzzer (+) | DC ne radi — mora PWM/ton |
| `PIN_MOTOR` | **17** | izlaz | baza NPN preko 1 kΩ | nikad direktno na motor! |
| `PIN_WAKE_BUTTON` | **21** | ulaz (`INPUT_PULLUP`) | momentary rocker → GND | pritisnut = LOW; ⚠️ vidi RTC napomenu |
| `PIN_STATUS_LED` | **48** | izlaz | onboard WS2812 RGB | opcionalno |

### ⚠️ Pinovi koje treba IZBJEGAVATI na ESP32-S3

- **GPIO0** — BOOT/strapping pin (ne koristiti za gumb)
- **GPIO45, GPIO46** — strapping pinovi
- **GPIO26–GPIO32** — rezervirani za SPI flash/PSRAM (N16R8 modul koristi octal PSRAM —
  i **GPIO33–GPIO37** mogu biti zauzeti; ne koristiti ih)
- **GPIO19, GPIO20** — USB D−/D+ (USB CDC)
- pinovi koji su samo ulazni ili nedostupni na headeru ploče

### ⚠️ RTC wake napomena (deep sleep)

Za EXT0 buđenje iz deep sleepa pin mora biti **RTC-capable**. Na ESP32-S3 RTC GPIO su
**GPIO0–GPIO21** → GPIO21 *jest* RTC-capable i zadani izbor je valjan.
Ako se na ploči pokaže nepouzdan (npr. interni pull-up oslabi u deep sleepu), dodati
**vanjski 10 kΩ pull-up s pina na 3.3 V** ili odabrati drugi RTC pin (1–18) u `config.h`.

---

## 1. Potenciometar B10K (emulator protoka daha)

```txt
        3V3 ──────┬─────────────┐
                  │             │
              ┌───┴───┐         │
              │ B10K  │         │
   GPIO4 ◄────┤ wiper │      ESP32-S3
              │       │         │
              └───┬───┘         │
                  │             │
        GND ──────┴─────────────┘
```

| Pin potenciometra | Spoj |
|---|---|
| vanjska noga 1 | 3V3 |
| vanjska noga 2 | GND |
| srednja noga (wiper) | GPIO4 (`PIN_POT_ADC`) |

- ADC 12-bit, raspon 0–4095; **upotrebljivi raspon 200–3900** zbog nelinearnosti rubova
- Centar (~2048) se **kalibrira pri startu** (prosjek ~1 s uzoraka u mirovanju), ne hardkodira
- Deadzone ±70 ADC counts oko kalibriranog centra → protok = 0

## 2. Pasivni buzzer

```txt
   GPIO15 ────────► buzzer (+)
   GND    ────────► buzzer (−)
```

- Buzzer je **pasivan**: na DC samo klikće → ton se generira **LEDC PWM-om**
- Direktan spoj na GPIO je prihvatljiv za male piezo struje; po potrebi dodati mali
  serijski otpornik (330 Ω) ili tranzistorski driver
- Zvukovi (sve non-blocking, bez `delay()`): start beep, beep pri ulasku u zonu,
  success melodija, error beep (fail / > 1200 ml/s)

## 3. Vibracijski motor (3 V nominalno, napajan s 5 V) — NPN low-side switch

> ⚠️ **NAJRIZIČNIJI DIO OŽIČENJA — pročitati cijelo prije spajanja.**
> Motor se NIKAD ne spaja direktno na GPIO niti na 3V3. Prvi hardverski test raditi s
> `ENABLE_MOTOR 0`; uključiti tek nakon vizualne provjere spoja.

```txt
   vanjski +5 V ──────────┬──────────────┐
                          │              │
                          │         ┌────┴────┐  katoda (pruga na diodi)
                      ┌───┴───┐     │ 1N4007/ │
                      │ MOTOR │     │ 1N4148  │  flyback dioda
                      └───┬───┘     └────┬────┘  anoda
                          │              │
                          ├──────────────┘
                          │ kolektor (C)
                          │
   GPIO17 ──[ 1 kΩ ]──┬───┤ NPN  (2N2222 / PN2222A / BC337)
                      │   │
                  [ 10 kΩ ]│ emiter (E)
                      │   │
   GND ───────────────┴───┴───────────────── GND vanjskog napajanja
                                              (ZAJEDNIČKI GND OBAVEZAN!)
```

| Element | Spoj | Svrha |
|---|---|---|
| motor (+) | vanjski +5 V | napajanje motora odvojeno od ESP32 GPIO |
| motor (−) | kolektor NPN | low-side prekidač |
| emiter NPN | GND | |
| GPIO17 | → **1 kΩ** → baza NPN | ograničenje struje baze (rezervno 330 Ω ako je motor preslab) |
| **10 kΩ** | baza → GND (pull-down) | drži tranzistor **OFF** tijekom reseta/boota/crasha/floating GPIO |
| flyback dioda | katoda → motor (+), anoda → motor (−)/kolektor | gasi induktivne šiljke pri isključenju |
| kondenzator ≥100 µF (opc.) | preko +5 V/GND blizu motora | smanjuje propade napona i šum |

### Pinout tranzistora (provjeriti datasheet svoje izvedbe!)

| Tranzistor (TO-92, ravna strana prema sebi, noge dolje) | 1 | 2 | 3 |
|---|---|---|---|
| PN2222A / 2N2222A (TO-92) | E | B | C |
| BC337 | C | B | E |

> ⚠️ PN2222A i BC337 imaju **obrnut** raspored! Krivo okrenut tranzistor = motor stalno
> uključen ili ne radi.

### Softverska zaštita (implementirano u firmwareu)

- **Prva instrukcija u `setup()`**: `PIN_MOTOR` → LOW
- `MOTOR_MAX_PULSE_MS 250` — tvrdi limit trajanja pulsa
- `MOTOR_COOLDOWN_MS 500` — pauza između pulseva
- `MOTOR_PWM_DUTY_MAX 90` (od 255, ~35 %) — ako se koristi PWM
- Pulsevi: upozorenje 80 ms · kolizija 150 ms · fail 250 ms — **nikad trajno uključen**

## 4. Wake/start gumb (momentary rocker)

```txt
   GPIO21 ────────► rocker switch ────────► GND
```

- `pinMode(PIN_WAKE_BUTTON, INPUT_PULLUP)` → pritisnut = **LOW**
- Funkcije: start vježbe, skip Wi-Fi portala, buđenje iz deep sleepa
  (`esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_WAKE_BUTTON, 0)`)
- Debounce u softveru (~30 ms), bez blokiranja

## 5. Napajanje

| Uređaj | Napajanje | Napomena |
|---|---|---|
| Controller (Master) | AC/DC PSU **5 V 1 A** → pin 5V/VIN | isti PSU napaja i motor kroz tranzistor |
| Motor | isti vanjski +5 V | **zajednički GND s ESP32 obavezan** |
| Display (Slave) | **3.3 V baterija** | |

### ⚠️ Kritična upozorenja

1. **NIKAD 5 V u 3V3 pin** — 3V3 pin je izlaz regulatora, 5 V ga uništava.
2. **Zajednički GND** između ESP32, vanjskog PSU-a i motora je **obavezan** — bez njega
   tranzistor nema referencu i motor se ponaša nasumično.
3. Motor na 5 V (nominalno 3 V) → **samo kratki pulsevi**, duty ograničen u softveru.
4. Pri prvom spajanju: prvo provjeriti spoj multimetrom (baza-pull-down, orijentacija
   diode, orijentacija tranzistora), tek onda `ENABLE_MOTOR 1`.

## 6. Display (Slave) ES3C28P

Display ploča ima **integriran** 2.8" 240×320 ILI9341V ekran (4-wire SPI) — nema
vanjskog ožičenja ekrana. Pinovi LCD-a se definiraju u
`display/include/display_config.h` prema **LCDWiki ES3C28P/ES3N28P** demo kodu.

> TODO markeri u `display_config.h` pokazuju točno koje vrijednosti prepisati iz
> službenog LCDWiki Arduino primjera (MOSI/MISO/SCLK/CS/DC/RST/BL).

## 7. Checklist prije prvog uključivanja

- [ ] Potenciometar: 3V3 / GND na vanjskim nogama, wiper na GPIO4
- [ ] Buzzer polaritet: + na GPIO15, − na GND
- [ ] Tranzistor orijentiran prema datasheetu (PN2222A ≠ BC337!)
- [ ] 1 kΩ između GPIO17 i baze
- [ ] 10 kΩ između baze i GND
- [ ] Flyback dioda: pruga (katoda) na motor (+)
- [ ] Zajednički GND: ESP32 ↔ PSU ↔ motor
- [ ] NIŠTA na 3V3 osim potenciometra
- [ ] `ENABLE_MOTOR 0` u `config.h` za prvi test
