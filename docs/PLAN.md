# SpiroBird — Detaljni plan projekta

> **Kolegij:** Razvoj ugradbenih sustava
> **Projekt:** SpiroBird — IoT digitalni spirometar u obliku igre u stilu Flappy Bird
> **Datum plana:** 2026-06-05
> **Status:** Plan — implementacija slijedi po fazama opisanim u nastavku

---

## 1. Sažetak projekta

SpiroBird je sustav od **dva fizička ESP32-S3 uređaja** koji emulira digitalni spirometar.
Potenciometar emulira protok daha. Korisnik "diše" (okreće potenciometar) i pokušava
održati stabilan protok u ciljnoj zoni **900–1200 ml/s** neprekidno **5 sekundi**.
Vizualizacija je igra u stilu Flappy Bird na zasebnom ESP32-S3 display uređaju.

Ovo **nije Wokwi-only simulacija** — sustav radi na stvarnom fizičkom hardveru i mora
biti pouzdan tijekom obrane projekta uživo.

### Medicinski pragovi (fiksni, ne mijenjati)

| Prag | Vrijednost | Značenje |
|---|---|---|
| Donji prag | 600 ml/s | Donja referentna linija na ekranu |
| Ciljna zona — početak | 900 ml/s | Minimalni protok za uspjeh |
| Ciljna zona — kraj / opasnost | 1200 ml/s | Iznad → trenutni neuspjeh (FAIL_OVER_1200) |

### Uvjet uspješne vježbe

1. Protok ≥ 900 ml/s
2. Protok ≤ 1200 ml/s
3. Protok stabilan unutar zone (peak-to-peak varijacija < 180 ml/s u prozoru od 500 ms)
4. Uvjeti vrijede **neprekidno 5000 ms**

Ako protok prijeđe 1200 ml/s → pokušaj odmah propada i resetira se.

---

## 2. Tim i odgovornosti

| Član | Uloga |
|---|---|
| Hrvoje Renato Šokčić | Razvoj softverskog rješenja (radi sve) |
| Sven Gavranović | Dokumentacija |
| Ivan Benčić | Simulacija |
| Domagoj Lepen | Testiranje |

---

## 3. Bodovanje i obavezni materijali (pravila kolegija)

| Stavka | Bodovi | Kako se pokriva ovim planom |
|---|---|---|
| Odgovaranje na pitanja i diskusija o projektu | 3 | Sekcija 14: priprema za obranu — popis očekivanih pitanja + tehnička dokumentacija (`docs/`) iz koje se uči |
| Prezentacija (.ppt/.pptx/.pdf) **committana u Git repozitorij** | 2 | Faza 9: Marp markdown prezentacija (`docs/presentation/presentation.md`) + eksport u PDF/PPTX, oboje committano |


---

## 4. Arhitektura sustava

```txt
┌──────────────────────────┐         ESP-NOW broadcast          ┌──────────────────────────┐
│  CONTROLLER / MASTER     │  ──── SpiroPacket @ 30–50 Hz ────► │  DISPLAY / SLAVE         │
│  ESP32-S3-N16R8 dev kit  │        (FF:FF:FF:FF:FF:FF)         │  ESP32-S3 ES3C28P 2.8"   │
│                          │                                    │  240x320 ILI9341V SPI    │
│  • potenciometar (ADC)   │                                    │                          │
│  • buzzer (LEDC PWM)     │                                    │  • prima pakete          │
│  • vibro motor (NPN)     │                                    │  • skenira Wi-Fi kanale  │
│  • state machine         │                                    │  • renderira igru        │
│  • NVS high score        │                                    │  • NE spaja se na Wi-Fi  │
│  • Wi-Fi + captive portal│                                    │  • NE šalje na server    │
│  • deep sleep + wake gumb│                                    │  • bez deep sleepa       │
└──────────┬───────────────┘                                    └──────────────────────────┘
           │ HTTP POST (samo nakon SUCCESS/FAIL, timeout 1500 ms)
           ▼
┌──────────────────────────┐
│  BACKEND (Node.js/Express)│
│  lokalno → Render.com    │
│  results.json + dashboard│
└──────────────────────────┘
```

### Ključne arhitekturne odluke (i zašto)

1. **Controller je jedini izvor istine za logiku igre.** Display samo renderira ono što
   primi. Time je logika testabilna bez ekrana (Serial), a ekran zamjenjiv.
2. **Samo Controller ima Wi-Fi vezu i šalje rezultate na server.** Display se nikad ne
   spaja na Wi-Fi, ne bira mrežu, ne sprema vjerodajnice.
3. **Wi-Fi provisioning preko captive portala na Controlleru** (WiFiManager ili vlastiti
   fallback AP `SpiroBird-Setup`), jer Controller nema ekran. Blokiranje je dopušteno
   *samo* u setup/boot fazi, nikad tijekom vježbe.
4. **ESP-NOW broadcast bez hardkodiranih MAC adresa.** Controller šalje na broadcast MAC;
   Display skenira kanale 1–13 dok ne primi ≥3 valjana paketa (magic + verzija +
   checksum), zatim se zaključa na kanal. Gubitak paketa > 3 s → ponovno skeniranje.
5. **ESP-NOW kanal = Wi-Fi kanal Controllera.** Controller se prvo spoji na Wi-Fi, očita
   aktivni kanal, pa inicijalizira ESP-NOW na njemu. Kanal se šalje u svakom paketu.
6. **Offline mod je prvoklasan:** ako Wi-Fi/server padne, igra radi lokalno, high score
   se sprema u NVS, Serial ispisuje `Wi-Fi unavailable, continuing offline`.
7. **Sve opcionalno iza compile-time zastavica** (`ENABLE_WIFI`, `ENABLE_MOTOR`, …) —
   svaki podsustav se može isključiti bez rušenja ostatka.
8. **Bez blokirajućeg `delay()`** u glavnoj logici — `millis()` raspoređivanje, ISR
   (ako se koristi) postavlja samo volatile zastavicu.
9. **Sleep arhitektura:** Controller ima pseudo sleep (60 s neaktivnosti) i pravi deep
   sleep (180 s, buđenje EXT0 na wake gumbu). Display **nikad** ne ide u deep sleep —
   samo pseudo sleep ekran i nastavak slušanja ESP-NOW-a (ESP-NOW ne može probuditi
   uređaj iz deep sleepa).

### Prioriteti dizajna (redoslijed)

1. Stabilan rad na fizičkom hardveru
2. Jednostavno debugiranje (Serial logovi, jasne tranzicije stanja)
3. Pouzdana Controller→Display komunikacija
4. Pouzdana logika senzora/igre
5. Napredne značajke samo ako ne ugrožavaju jezgru

**Bez overengineeringa.** Minimalni radni demo: potenciometar → protok/stabilnost/volumen
→ ESP-NOW paketi → render igre + buzzer/motor + POST rezultata ako je server dostupan.

### Što se NE implementira u prvoj verziji

Odabir Wi-Fi mreže na Displayu, unos lozinke na ekranu, touch izbornici, baze s
autentikacijom, korisnički računi, cloud login, deep sleep Displaya, enkripcija
komunikacije, Bluetooth, OTA, učitavanje asseta s SD kartice.

---

## 5. Hardver i ožičenje (sažetak — detalji idu u `docs/wiring.md`)

### Controller pin mapping (zadane vrijednosti, sve konfigurabilno u `config.h`)

```cpp
#define PIN_POT_ADC       4    // ADC — srednji pin (wiper) potenciometra B10K
#define PIN_BUZZER        15   // pasivni buzzer, LEDC PWM
#define PIN_MOTOR         17   // baza NPN tranzistora preko 1 kΩ
#define PIN_WAKE_BUTTON   21   // momentary rocker prema GND, INPUT_PULLUP, RTC wake
#define PIN_STATUS_LED    48   // onboard WS2812 RGB (opcionalno)
```

> ⚠️ **PROVJERITI NA HARDVERU:** GPIO21 mora biti RTC-capable za EXT0 wake na ovoj ploči
> (DUBEUYEW ESP32-S3-N16R8). Ako nije — odabrati drugi izloženi RTC GPIO i promijeniti u
> `config.h`. Izbjegavati GPIO0 (strapping). Po potrebi vanjski 10 kΩ pull-up na 3.3 V.

### Ožičenje po komponenti

| Komponenta | Spoj |
|---|---|
| Potenciometar B10K | vanjska noga → 3V3, druga vanjska → GND, wiper → GPIO4 |
| Pasivni buzzer | GPIO15 → buzzer (+), buzzer (−) → GND; PWM/LEDC ton (DC samo klikće) |
| Vibro motor (3 V nom., napaja se 5 V u kratkim pulsevima) | +5 V → motor (+); motor (−) → kolektor NPN (2N2222/PN2222A/BC337); emiter → GND; GPIO17 → **1 kΩ** → baza; **10 kΩ pull-down baza→GND** (drži tranzistor OFF pri resetu/bootu); flyback dioda 1N4007/1N4148 preko motora (katoda na +, anoda na −/kolektor); opc. ≥100 µF kondenzator uz napajanje motora |
| Wake gumb | GPIO21 → momentary rocker → GND, INPUT_PULLUP (pritisnut = LOW) |
| Napajanje Master | AC/DC PSU 5 V 1 A → ESP32 5V/VIN + motor; **zajednički GND obavezan** |
| Napajanje Slave (display) | 3.3 V baterija |

> ⚠️ **NIKAD 5 V u 3V3 pin. NIKAD motor direktno na GPIO ili 3V3.**

### Softverska zaštita motora

- Prvi redak `setup()`: motor pin **LOW**
- `MOTOR_MAX_PULSE_MS 250` (hard guard), `MOTOR_COOLDOWN_MS 500`, `MOTOR_PWM_DUTY_MAX 90/255`
- Zadano `ENABLE_MOTOR 0` dok se tranzistorski sklop ne verificira na hardveru
- Pulsevi: upozorenje 80 ms, kolizija 150 ms, fail 250 ms — nikad trajno uključen

### Obrada ADC signala

- 12-bit (0–4095); upotrebljivi raspon `ADC_MIN_USABLE 200` … `ADC_MAX_USABLE 3900`
  (nelinearnost rubova)
- Kalibracija centra pri startu: prosjek ~1 s uzoraka u mirovanju (očekivano ~2048, ali
  **ne hardkodirati**)
- `ADC_DEADZONE 70` oko centra → flow = 0
- Mapiranje: `deviation = |raw − offset|` → 0…1400 ml/s (`FLOW_MAX_ML_S 1400`), clamp
- Usrednjavanje 4–8 uzoraka po čitanju + EMA filter (`EMA_ALPHA 0.20`)
- Stabilnost: ring buffer ~500 ms filtriranih vrijednosti, peak-to-peak < 180 ml/s

---

## 6. Struktura repozitorija

```txt
SpiroBird/
├── controller/                  # PlatformIO projekt — MASTER firmware
│   ├── platformio.ini
│   ├── include/
│   │   ├── config.h             # SVE konstante, pinovi, ENABLE_* zastavice
│   │   ├── protocol.h           # zajednički SpiroPacket (identičan na obje strane)
│   │   ├── types.h
│   │   └── secrets.example.h    # predložak (server URL); pravi secrets.h u .gitignore
│   └── src/
│       ├── main.cpp             # setup/loop, raspoređivanje, state machine glue
│       ├── BreathSensor.{h,cpp} # ADC, kalibracija, deadzone, EMA, stabilnost
│       ├── ExerciseLogic.{h,cpp}# state machine, volumen, success/fail
│       ├── Haptics.{h,cpp}      # buzzer + motor, non-blocking
│       ├── EspNowSender.{h,cpp} # broadcast, seq, checksum
│       ├── WifiProvisioning.{h,cpp} # WiFiManager / fallback AP portal
│       ├── ServerClient.{h,cpp} # HTTP POST rezultata, timeout 1500 ms
│       └── Storage.{h,cpp}      # NVS/Preferences: high score, Wi-Fi vjerodajnice
├── display/                     # PlatformIO projekt — SLAVE firmware
│   ├── platformio.ini
│   ├── include/
│   │   ├── config.h
│   │   ├── protocol.h           # kopija — mora ostati bajt-identičan
│   │   └── display_config.h     # ES3C28P pinovi (TODO markeri za LCDWiki vrijednosti)
│   └── src/
│       ├── main.cpp
│       ├── EspNowReceiver.{h,cpp} # channel scan/lock, validacija paketa
│       ├── GameRenderer.{h,cpp}   # sprite double-buffer, ptica, fizika, zone
│       └── UiScreens.{h,cpp}      # NO SIGNAL / Wi-Fi setup / rezultat / sleep ekrani
├── server/                      # Node.js backend (Render-kompatibilan)
│   ├── package.json
│   ├── server.js
│   ├── data/results.json
│   ├── public/{index.html,style.css,app.js}   # dashboard
│   └── README.md
├── sim/                         # simulacija i testovi PRIJE flashanja
│   ├── simulate_breath.py
│   ├── test_logic.py
│   ├── sample_packets.json
│   └── flappy_preview.html      # opcionalni browser preview
├── docs/
│   ├── PLAN.md                  # ovaj dokument
│   ├── wiring.md                # detaljne tablice ožičenja + sheme
│   ├── protocol.md              # SpiroPacket, checksum, channel scan
│   ├── testing-plan.md          # bring-up koraci 1–7 + checkliste
│   ├── troubleshooting.md
│   ├── case-design.md           # 3D printano kućište
│   └── presentation/
│       ├── presentation.md      # Marp izvor
│       └── presentation.pdf     # eksport — COMMITAN u repo (2 boda)
├── .gitignore
└── README.md                    # naglasiti: fizički hardver, ne Wokwi-only
```

---

## 7. Protokol podataka (detalji u `docs/protocol.md`)

- Zajednički `protocol.h`, **packed struct**, `SPIROBIRD_MAGIC 0x5342`, verzija 1
- `SpiroPacket`: magic, version, seq, timestampMs, rawAdc, deviationAdc, flowMlS,
  filteredFlowMlS, volumeMl, maxFlowMlS, avgFlowMlS, stableTimeMs, state, failReason,
  targetZone/dangerZone/success/fail, wifiStatus, serverStatus, **espNowChannel**, checksum
- Checksum: jednostavna XOR/sum suma svih bajtova prije checksum polja — Display odbacuje
  nevaljane pakete i broji greške
- Stanja: `STATE_IDLE, CALIBRATING, READY, ACTIVE, SUCCESS, FAIL, RESULT, STATE_SLEEP`
- Fail razlozi: `FAIL_NONE, FAIL_OVER_1200, FAIL_UNSTABLE, FAIL_COLLISION, FAIL_TIMEOUT`
- Statusne poruke koje Display smije prikazati (samo ono što Controller pošalje):
  `Wi-Fi setup mode`, `Connect to SpiroBird-Setup`, `Open 192.168.4.1`, `Wi-Fi connected`,
  `Server online/offline`, `Playing Offline`

### State machine (Controller)

```txt
IDLE ──(gumb/protok)──► CALIBRATING ──(offset spremljen)──► READY
READY ──(protok > prag)──► ACTIVE
ACTIVE ──(stabilno 5000 ms)──► SUCCESS ──► RESULT ──(timeout)──► IDLE
ACTIVE ──(flow > 1200 ili raw > 1250)──► FAIL ──► RESULT ──(timeout)──► IDLE
bilo koje ──(60 s neaktivnosti)──► STATE_SLEEP (pseudo) ──(180 s)──► deep sleep
```

Svaka tranzicija se ispisuje na Serial: `STATE_ACTIVE -> STATE_SUCCESS`.

### Vremenske konstante glavne petlje

| Zadatak | Frekvencija |
|---|---|
| ADC uzorkovanje | 100 Hz (svakih 10 ms) |
| ESP-NOW slanje | 30–50 Hz |
| Server POST | samo nakon završetka pokušaja |
| Buzzer/motor update | svaka iteracija, non-blocking |
| Wi-Fi reconnect | periodički, nikad tijekom ACTIVE |
| Display render | 25–30 FPS, TFT_eSprite double buffer |

---

## 8. Backend (Node.js / Express / Render)

- Endpointi: `GET /health`, `POST /api/results`, `GET /api/results`,
  `GET /api/highscore`, `DELETE /api/results` (test)
- Pohrana: `data/results.json` (file-based; u dokumentaciji jasno navesti da je na
  Renderu efemerna i da bi produkcija koristila pravu bazu)
- Dashboard (`public/`): kartice (najbolji volumen, zadnji rezultat, broj uspjeha),
  tablica pokušaja, auto-refresh svakih nekoliko sekundi
- `PORT` iz okoline, `npm start`, CORS, JSON body parser
- ESP32 strana: `http.setTimeout(1500)`, POST samo nakon SUCCESS/FAIL, greška → log i
  nastavi (nikad ne smrzava sustav)

---

## 9. Plan implementacije po fazama

Prošireno za prezentaciju i pripremu obrane.

### Faza 0 — Inicijalizacija repozitorija ✅ (ova sesija)
- `git init`, `.gitignore` (PlatformIO `.pio/`, `secrets.h`, `node_modules/`, …)
- `docs/PLAN.md` (ovaj dokument)
- **Izlaz:** prvi commit

### Faza 1 — Arhitektura i dokumentacijski kostur
- `README.md` (naglasak: fizički hardver), `docs/wiring.md`, `docs/protocol.md`
- **Izlaz:** kompletna tehnička dokumentacija prije koda

### Faza 2 — Zajednički protokol
- `protocol.h` (identičan u `controller/include` i `display/include`), checksum funkcija
- **Provjera:** `sizeof(SpiroPacket)` ispisan na obje strane mora biti identičan

### Faza 3 — Controller firmware (jezgra, bez mreže)
- `platformio.ini`, `config.h` (svi pinovi + ENABLE_* zastavice), `types.h`
- `BreathSensor` (kalibracija, deadzone, EMA, ring buffer stabilnosti)
- `ExerciseLogic` (state machine, volumen integracijom, success/fail)
- `Haptics` (LEDC buzzer melodije + motor pulsevi s guardovima, sve non-blocking)
- `Storage` (NVS high score)
- `main.cpp` s `millis()` schedulerom; motor LOW kao prva instrukcija `setup()`
- **Provjera:** kompilira se s `ENABLE_WIFI 0`, `ENABLE_ESPNOW 0`, `ENABLE_MOTOR 0`;
  Serial ispisuje raw ADC, offset, flow, tranzicije stanja (= bring-up korak 1)

### Faza 4 — Controller mreža
- `EspNowSender` (broadcast, seq, kanal u paketu), `WifiProvisioning`
  (WiFiManager + fallback vlastiti AP portal; timeouti `WIFI_CONNECT_TIMEOUT_MS 8000`,
  `WIFI_PORTAL_TIMEOUT_SEC 180`; skip gumbom na GPIO21), `ServerClient`
- Offline mod: portal timeout ili gumb → `OFFLINE_MODE` status Displayu
- **Provjera:** kompilira sa svim zastavicama uključenim/isključenim u svim kombinacijama

### Faza 5 — Display firmware
- `platformio.ini` (TFT_eSPI ili LovyanGFX — odlučiti prema ES3C28P podršci),
  `display_config.h` s **TODO markerima** za točne LCDWiki pinove + upute odakle ih
  prepisati
- `EspNowReceiver` (channel scan 1–13, lock nakon 3 valjana paketa, re-scan nakon 3 s
  tišine), `UiScreens` (NO SIGNAL/SCANNING CH n, Wi-Fi setup, offline, sleep, rezultat)
- `GameRenderer`: sprite double buffer, ptica (geometrijski pixel-art), linije
  600/900/1200, HUD (flow, volumen, stable timer, state, Wi-Fi/server status),
  fizika s inercijom (`spring`/`damping` konfigurabilni), 25–30 FPS
- `ENABLE_DISPLAY_FAKE_DATA_MODE 1` → demo animacija bez ESP-NOW (= bring-up korak 5)
- **Provjera:** kompilira; fake mod renderira igru bez Controllera

### Faza 6 — Backend
- `server.js` + dashboard + `data/results.json` + Render upute
- **Provjera:** `npm start` lokalno; curl testovi `GET /health` i `POST /api/results`
  (= bring-up korak 4) — **ovo mogu stvarno izvršiti i verificirati u ovoj sesiji**

### Faza 7 — Simulacija i testovi
- `sim/test_logic.py`: EMA, mapiranje protoka, stabilnost, integracija volumena,
  uspjeh nakon 5 s, fail iznad 1200 — port C++ logike u Python 1:1 s istim konstantama
- `sim/simulate_breath.py`: scenariji (stabilan uspjeh ~1000 ml/s, fail >1200,
  nestabilan, preslab protok) → očekivane tranzicije + `sample_packets.json`
- `sim/flappy_preview.html`: browser pregled ptice iz sample paketa
- **Provjera:** svi testovi prolaze lokalno **prije** flashanja (= zadaća Ivana — simulacija)

### Faza 8 — Dokumentacija (finalizacija)
- `docs/testing-plan.md` — formaliziran obavezni bring-up redoslijed (sekcija 10 dolje)
  s checklistama za Domagoja (testiranje)
- `docs/troubleshooting.md` — poznati problemi (ESP-NOW kanal, ADC šum, WiFiManager na
  S3, USB CDC, motor) + rješenja
- `docs/case-design.md` — smjernice za 3D kućište
- **Izlaz:** materijal za Svena (dokumentacija)

### Faza 9 — Prezentacija (2 boda)
- `docs/presentation/presentation.md` (Marp, hrvatski): motivacija, arhitektura,
  hardver/ožičenje, protokol, state machine, demo plan, rezultati testiranja, podjela
  posla po članovima
- Eksport u PDF (`marp presentation.md --pdf`) i commit **oba** artefakta
- **Izlaz:** prezentacija u Git repozitoriju ✔

### Faza 10 — Upute za flashanje, debug i obranu
- U README: točne `pio run`/`pio run -t upload`/`pio device monitor` naredbe za oba
  uređaja, pokretanje servera, deploy na Render, test portala/ESP-NOW/senzora/buzzera/
  motora/scenarija uspjeha i neuspjeha
- Checklist što korisnik javlja nakon testa na stvarnom hardveru (sekcija 11 dolje)
- Priprema za obranu: lista očekivanih pitanja s odgovorima (3 boda — diskusija)

---

## 10. Obavezni bring-up redoslijed na stvarnom hardveru

**Strogo ovim redom — ne pretpostavljati da sve radi odjednom:**

1. **Controller standalone** — samo Serial: boot info, raw ADC, kalibrirani offset,
   protok se mijenja okretanjem potenciometra. Bez displaya, servera, motora.
2. **Buzzer** — LEDC tonovi: start beep, success melodija, error beep. Bez `delay()`.
3. **Motor** — zadano isključen; prvo vizualna provjera tranzistorskog spoja, zatim
   80 ms puls, pa 250 ms fail puls. Nikad kontinuirano.
4. **Backend** — lokalno `npm start` → `GET /health` → `POST /api/results` curl-om →
   POST s ESP32 → deploy na Render.
5. **Display standalone** — test ekran + fake SpiroBird animacija bez ESP-NOW;
   verifikacija drivera i orijentacije (landscape 320×240 ako moguće).
6. **ESP-NOW** — oba uređaja: Controller broadcasta, Display skenira, lockira kanal,
   Serial pokazuje brojač paketa, ekran pokazuje živi protok.
7. **Puna igra** — slab protok, ciljna zona, 5 s uspjeh, >1200 fail, volumen, POST
   rezultata.

---

## 11. Checklist za povratnu informaciju nakon testa uživo

Korisnik nakon svakog testa javlja:

- [ ] Serial logovi (boot, kalibracija, tranzicije stanja)
- [ ] Pojavljuje li se Wi-Fi portal `SpiroBird-Setup`
- [ ] Spaja li se Controller na Wi-Fi i **koji kanal** koristi
- [ ] Pronalazi li Display taj kanal i lockira li se
- [ ] Stižu li ESP-NOW paketi (brojač, checksum greške)
- [ ] Inicijalizira li se Display (driver, orijentacija, boje)
- [ ] Mijenja li se protok na ekranu pri okretanju potenciometra
- [ ] Radi li buzzer (start/uspjeh/greška)
- [ ] Pulsira li motor (tek nakon verifikacije ožičenja)
- [ ] Prima li server POST (status kod)
- [ ] Rade li success (5 s) i fail (>1200) scenariji

---

## 12. Rizici i mitigacije

| # | Rizik | Vjerojatnost | Mitigacija |
|---|---|---|---|
| 1 | ES3C28P pinovi/driver nepoznati → display ne radi | Visoka | Izolirani `display_config.h` s TODO markerima; upute za prepisivanje iz LCDWiki demo koda; fake-data mod za test bez ESP-NOW; alternativa LovyanGFX ako TFT_eSPI ne uspije |
| 2 | ESP-NOW i Wi-Fi kanal se ne poklapaju | Srednja | Kanal u svakom paketu; Display channel-scan/lock/re-scan; dokumentirani UDP fallback (oba na istom Wi-Fi-ju) ako ESP-NOW bude nestabilan |
| 3 | WiFiManager nekompatibilan s ESP32-S3/PlatformIO | Srednja | Vlastiti fallback provisioning (AP + jednostavna web stranica sa skenom mreža); `ENABLE_WIFI_MANAGER` zastavica |
| 4 | Motor 3 V na 5 V pregrijavanje / GPIO oštećenje | Srednja | NPN low-side + flyback dioda + 10 kΩ pull-down; duty ≤ 90/255; max puls 250 ms + cooldown; `ENABLE_MOTOR 0` dok sklop nije verificiran; motor LOW kao prva instrukcija u `setup()` |
| 5 | ADC šum/nelinearnost → nemoguće držati stabilnu zonu | Srednja | Usable range 200–3900, usrednjavanje 4–8 uzoraka, EMA, deadzone; svi pragovi podesivi u `config.h` |
| 6 | GPIO21 nije RTC-capable → deep sleep wake ne radi | Niska/Srednja | Konfigurabilan pin; rezervni RTC pinovi dokumentirani; `ENABLE_SLEEP_MODE` se može isključiti bez utjecaja na jezgru |
| 7 | Render free tier — spavanje servera / efemeran disk | Visoka (poznato) | HTTP timeout 1500 ms + offline mod; dokumentirano da je file storage demo rješenje |
| 8 | WiFiManager portal blokira igru | Po dizajnu | Portal samo u boot fazi; timeout 180 s ili skip gumbom; Display prikazuje status |
| 9 | Ponestane vremena prije obrane | — | Bring-up redoslijed daje radni demo već nakon koraka 6; server/sleep/motor su opcionalni dodaci |

---

## 13. Konfiguracijske zastavice (jezgra mora raditi bez opcionalnog)

```cpp
#define ENABLE_WIFI 1
#define ENABLE_WIFI_MANAGER 1
#define ENABLE_SERVER_POST 1
#define ENABLE_ESPNOW 1
#define ENABLE_ESPNOW_CHANNEL_SCAN 1
#define ENABLE_MOTOR 1            // 0 za prvi hardverski test!
#define ENABLE_BUZZER 1
#define ENABLE_SLEEP_MODE 0       // uključiti tek kad jezgra radi
#define ENABLE_DEBUG_SERIAL 1
#define ENABLE_OFFLINE_MODE 1
#define ENABLE_DISPLAY_FAKE_DATA_MODE 1
```

Garancije: Wi-Fi padne → igra radi; server padne → igra radi; motor isključen → igra
radi; display nije spojen → Controller ispisuje logiku na Serial; šum potenciometra →
pragovi se podešavaju u `config.h`.

---

## 14. Priprema za obranu (3 boda — pitanja i diskusija)

Materijal za učenje: `docs/` + ovaj plan. Očekivana pitanja koja dokumentacija mora
pokriti (i koja idu u prezentacijske backup slajdove):

1. Zašto ESP-NOW, a ne MQTT/HTTP/Bluetooth između uređaja?
2. Kako rješavate problem ESP-NOW kanala kad je Controller na kućnom Wi-Fi-ju?
3. Zašto Display ne ide u deep sleep? (ESP-NOW ne može probuditi uređaj iz deep sleepa)
4. Kako štitite 3 V motor na 5 V napajanju? (hardverski + softverski)
5. Zašto kalibrirate ADC offset umjesto hardkodiranja 2048?
6. Kako je izveden non-blocking dizajn? (millis scheduler, ISR samo zastavica)
7. Kako računate volumen? (numerička integracija `flow × dt`)
8. Što se događa kad Wi-Fi/server nije dostupan? (offline mod)
9. Koji su elementi ugradbenih sustava demonstrirani? (interrupti, event-driven,
   sleep/power management, NVS, watchdog-style guardovi, ADC, PWM/LEDC)
10. Kako ste testirali prije flashanja? (Python simulacija identične logike)

---

## 15. Definicija gotovog (Definition of Done)

- [ ] Oba firmwarea se kompiliraju u PlatformIO sa svim kombinacijama ENABLE_* zastavica
- [ ] `sim/test_logic.py` — svi testovi prolaze
- [ ] Backend radi lokalno (verificirano curl-om) + upute za Render
- [ ] Bring-up koraci 1–7 dokumentirani s checklistama
- [ ] README + svi `docs/` fajlovi kompletni (hrvatski)
- [ ] Prezentacija (Marp `.md` + eksport `.pdf`) committana u repo
- [ ] Rizične pretpostavke (display pinovi, GPIO21 RTC, WiFiManager S3) jasno označene
      TODO markerima — projekt pošteno opisuje što je implementirano, a što testirano
