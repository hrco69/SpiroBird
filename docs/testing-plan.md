# SpiroBird — Plan testiranja / bring-up vodič

> **Obavezni redoslijed — ne preskakati korake.** Svaki korak ima: cilj, spajanje,
> zastavice u `config.h`, naredbe, očekivani izlaz i što javiti ako ne radi.
>
> Zaduženja: Domagoj (testiranje) vodi checkliste; Hrvoje flasha i debugira.

## 0. Preduvjeti (jednokratno)

### PlatformIO u terminalu

PlatformIO je instaliran u `%USERPROFILE%\.platformio\penv\Scripts\pio.exe`.
Za udobnost u svakoj PowerShell sesiji:

```powershell
$env:Path += ";$env:USERPROFILE\.platformio\penv\Scripts"
pio --version    # provjera
```

(Alternativa: VS Code s PlatformIO ekstenzijom — gumbi Build/Upload/Monitor.)

### USB i COM port

- Koristiti **podatkovni** USB kabel (ne samo za punjenje!).
- Mnoge ESP32-S3 ploče imaju **dva USB-C porta**: `USB` (nativni) i `COM`/`UART`.
  Firmware ima `ARDUINO_USB_CDC_ON_BOOT=1` → **Serial izlaz ide na nativni `USB`
  port**. Upload obično radi preko oba.
- Provjera porta: `pio device list`
- Ako se ploča ne vidi: instalirati driver (CH340/CP210x za COM port; nativni
  USB ne treba driver na Win10/11).

### Ako upload ne uspijeva (uobičajeno na S3!)

1. Držati **BOOT**, kratko pritisnuti **RST**, pustiti BOOT → download mode
2. Ponoviti `pio run -t upload`
3. Nakon uploada pritisnuti **RST** jednom (ploča zna ostati u download modu)

### Zastavice po koracima (`controller/include/config.h`)

| Korak | WIFI | SERVER_POST | ESPNOW | BUZZER | MOTOR |
|---|---|---|---|---|---|
| 1–2 Standalone + buzzer (TRENUTNO) | 0 | 0 | 0 | 1 | 0 |
| 3 Motor | 0 | 0 | 0 | 1 | **1** (tek nakon provjere spoja!) |
| 4 Backend | **1** | **1** | 0 | 1 | po želji |
| 6 ESP-NOW | 1 | 1 | **1** | 1 | po želji |
| 7 Puna igra | 1 | 1 | 1 | 1 | 1 |

---

## Korak 1 — Controller standalone (potenciometar + Serial)

**Cilj:** ADC radi, kalibracija radi, protok se mijenja okretanjem potenciometra.

**Spajanje** (samo potenciometar):

| Potenciometar B10K | ESP32-S3 |
|---|---|
| vanjska noga 1 | 3V3 |
| vanjska noga 2 | GND |
| srednja noga (wiper) | GPIO4 |

**Naredbe:**

```powershell
cd C:\Users\hrcsi\Desktop\SpiroBird\controller
pio run -t upload
pio device monitor -b 115200
```

**Očekivani Serial izlaz:**

```txt
 SpiroBird Controller (MASTER) booting
[boot] chip: ESP32-S3 ...
[boot] pins: POT=4 BUZZER=15 MOTOR=17 WAKE_BTN=21 LED=48
[boot] sizeof(SpiroPacket)=48 bytes (must match Display!)
[storage] high score: bestVol=0 ml ...
[sensor] calibration started (1000 ms) — do not touch the potentiometer
[logic] STATE_IDLE -> STATE_CALIBRATING
[sensor] calibration done: offset=XXXX (expected ~2048) ...
[logic] STATE_CALIBRATING -> STATE_READY
[dbg] STATE_READY raw=2048 off=2048 dev=    0 flow=   0.0 filt=   0.0 ...
```

**Checklist:**
- [ ] Boot info se ispisuje
- [ ] Offset nakon kalibracije razuman (potenciometar u sredini → ~1800–2300;
      ako je skroz na kraju → blizu 200 ili 3900, to je isto OK)
- [ ] `raw` se mijenja glatko 200–3900 okretanjem potenciometra
- [ ] `flow` raste s otklonom od kalibriranog centra (u OBA smjera!)
- [ ] Protok > 150 → `STATE_READY -> STATE_ACTIVE`
- [ ] Držanjem u zoni 900–1200 raste `stable` brojač → SUCCESS nakon 5 s
- [ ] Naglo preko ~1200 → `STATE_ACTIVE -> STATE_FAIL`
- [ ] Onboard RGB LED mijenja boje sa stanjima (plava→ljubičasta→žuta→zelena)

**Ako ne radi, javiti:** cijeli Serial log od boota + opis što potenciometar radi.

---

## Korak 2 — Buzzer

**Cilj:** LEDC tonovi rade (pasivni buzzer mora dobiti PWM, na DC samo klikće).

**Spajanje:** buzzer (+) → GPIO15, buzzer (−) → GND. `ENABLE_BUZZER` je već 1.

**Test:** proći jednu vježbu:
- [ ] Kratki beep kad uđe u STATE_READY
- [ ] Pozitivni blip pri ulasku u zonu 900–1200
- [ ] Melodija (4 tona) na SUCCESS
- [ ] Dvotonski error beep na FAIL

**Ako ne radi:** zamijeniti + i − (piezo često radi u oba smjera, ali tiše);
provjeriti da je buzzer pasivni (aktivni svira sam na DC — onda tonovi zvuče krivo).

---

## Korak 3 — Motor (NAJOPREZNIJE!)

**Cilj:** kratki pulsevi vibracije kroz NPN tranzistor.

**Spajanje:** točno po `docs/wiring.md` (shema + tablica). **Prije uključivanja:**

- [ ] Tranzistor orijentiran po datasheetu (PN2222A: E-B-C; BC337: C-B-E — OBRNUTO!)
- [ ] 1 kΩ između GPIO17 i baze
- [ ] 10 kΩ između baze i GND
- [ ] Dioda: pruga (katoda) na motor (+) / +5 V stranu
- [ ] Zajednički GND: ESP32 GND ↔ GND vanjskog 5 V napajanja
- [ ] Motor NIJE spojen na 3V3 ni direktno na GPIO

**Tek tada** u `config.h`: `#define ENABLE_MOTOR 1` → rebuild + upload.

**Test:**
- [ ] Pri izlasku iz zone tijekom ACTIVE: kratki puls 80 ms
- [ ] Na FAIL: jači puls 250 ms
- [ ] Motor se NIKAD ne vrti kontinuirano
- [ ] Nakon reseta/boota motor miruje (10 kΩ pull-down drži tranzistor OFF)

**Ako motor stalno radi:** ODMAH isključiti napajanje → tranzistor je krivo
okrenut ili nedostaje pull-down.

---

## Korak 4 — Backend (lokalno → ESP32 → Render)

### 4a. Lokalni server

```powershell
cd C:\Users\hrcsi\Desktop\SpiroBird\server
npm install     # ako već nije
npm start       # http://localhost:3000
```

- [ ] `Invoke-RestMethod http://localhost:3000/health` → `{"status":"ok"}`
- [ ] Dashboard u browseru: http://localhost:3000

### 4b. Windows firewall (KRITIČNO za ESP32 → laptop!)

ESP32 mora doći do porta 3000 na laptopu. Jednokratno (admin PowerShell):

```powershell
New-NetFirewallRule -DisplayName "SpiroBird server 3000" -Direction Inbound -LocalPort 3000 -Protocol TCP -Action Allow
```

Saznati LAN IP laptopa: `ipconfig` → IPv4 (npr. `192.168.1.42`).
Test s mobitela (ista mreža): otvoriti `http://<laptop-ip>:3000` u browseru.

### 4c. secrets.h + ESP32 POST

```powershell
Copy-Item controller\include\secrets.example.h controller\include\secrets.h
```

U `secrets.h`: `#define SERVER_BASE_URL "http://<laptop-ip>:3000"`

U `config.h`: `ENABLE_WIFI 1`, `ENABLE_SERVER_POST 1` → rebuild + upload.

**Prvi boot s Wi-Fi-jem:**
1. Serial: `trying saved credentials...` → nema spremljenih → portal
2. Mobitelom se spojiti na AP **SpiroBird-Setup**
3. Otvoriti `http://192.168.4.1` → odabrati kućni Wi-Fi → upisati lozinku
4. Serial: `connected to '...', IP=..., channel=N` ← **zapisati kanal!**
5. `[server] health check ... ONLINE`

- [ ] Portal se pojavljuje i radi (ili: skip gumbom radi → offline mod)
- [ ] Odraditi jednu vježbu → Serial: `[server] POST ... -> 201`
- [ ] Rezultat vidljiv na dashboardu

**VAŽNO:** mobitel/laptop i ESP32 moraju biti na ISTOJ mreži. Sveučilišni
eduroam tipično NE radi (izolacija klijenata) — koristiti mobilni hotspot.

### 4d. Render deploy (kad lokalno radi)

1. Pushati repo na GitHub (vidi dolje — mogu pomoći pri postavljanju)
2. render.com → New → Web Service → spojiti repo
3. Root Directory: `server`, Build: `npm install`, Start: `npm start`, Free tier
4. Test: `https://<ime>.onrender.com/health`
5. `secrets.h` → `#define SERVER_BASE_URL "https://<ime>.onrender.com"` → reflash

---

## Korak 5 — Display standalone

**Cilj:** ekran radi, orijentacija dobra, fake demo se vrti (bez Controllera).

```powershell
cd C:\Users\hrcsi\Desktop\SpiroBird\display
pio run -t upload
pio device monitor -b 115200
```

**Očekivano:** boot → "SpiroBird / NO SIGNAL / SCANNING CH 1..13" → nakon 8 s
kreće **FAKE DATA demo** (cijela vježba u petlji s crvenim bannerom).

- [ ] Ekran se pali, pozadinsko svjetlo radi
- [ ] Slika je landscape, ničega zrcalnog/obrnutog
- [ ] Boje dobre (nebo tamnoplavo, ptica žuta)
- [ ] Fake demo animira pticu glatko (~30 FPS)
- [ ] Serial ispisuje SCANNING CH 1→13 u krug

**Ako je ekran bijel/crn/kriv:** vidjeti napomene na dnu
`display/include/display_config.h` (driver, rotacija, inverzija, SPI brzina) —
javiti točan simptom.

---

## Korak 6 — ESP-NOW komunikacija

**Cilj:** Display pronađe i zaključa kanal, prima žive podatke.

U controller `config.h`: `ENABLE_ESPNOW 1` → rebuild + upload. Oba uređaja uključena.

**Očekivano na Display Serialu:**

```txt
[rx] NO SIGNAL / SCANNING CH 1
...
[rx] LOCKED CH 6 — RECEIVING SPIROBIRD DATA
[dbg] LOCKED ch=6 rx=1234 (40/s) bad=0 fresh=1 state=2 flow=0
```

- [ ] Kanal na kojem je locked == kanal iz Controller Serial loga
- [ ] `rx` raste ~40/s, `bad=0` (ili vrlo malo)
- [ ] Okretanje potenciometra ODMAH miče pticu/brojku na ekranu
- [ ] Privremeno ugasiti Controller → "NO SIGNAL" unutar ~1 s, re-scan nakon 3 s
- [ ] Upaliti Controller → Display se sam ponovno zaključa

---

## Korak 7 — Puna igra (sve zajedno)

- [ ] Slab protok: ptica nisko, bez napretka
- [ ] Zona 900–1200: ptica u koridoru, stability bar puni se 0→5 s
- [ ] SUCCESS: melodija + zeleni ekran + rezultat + POST (201) + dashboard
- [ ] Preko 1200: crveno nebo → FAIL ekran s razlogom + error beep + motor puls
- [ ] Volumen na ekranu raste tijekom vježbe (≈ protok × vrijeme)
- [ ] High score se pamti nakon resa (Serial `[storage]` pri bootu)
- [ ] Bez Wi-Fi-ja (skip portala): sve isto radi, status "PLAYING OFFLINE"

---

## Što javiti nakon svakog testa (za debugging)

1. **Cijeli Serial log** od boota (copy-paste iz monitora)
2. Koji korak / koja checklista stavka je pala
3. Wi-Fi: pojavljuje li se portal, spaja li se, **koji kanal**
4. ESP-NOW: locked kanal, rx brojač, bad brojač
5. Display: točan simptom (bijel ekran / kriva orijentacija / krive boje / ništa)
6. Server: status kod POST-a, vidi li se rezultat na dashboardu
7. Fotografija spoja ako je hardversko (posebno motor!)
