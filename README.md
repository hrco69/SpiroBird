# SpiroBird 🐦💨

**IoT digitalni spirometar u obliku igre u stilu Flappy Bird**

> Kolegij: **Razvoj ugradbenih sustava**
>
> ⚠️ **Ovo je projekt za stvarni fizički hardver, a ne Wokwi-only simulacija.**
> Sustav radi na dva fizička ESP32-S3 uređaja. Simulacijski alati u `sim/` služe za
> verifikaciju logike *prije* flashanja, ne kao zamjena za hardver.

## Tim

| Član | Uloga |
|---|---|
| Hrvoje Renato Šokčić | Razvoj softverskog rješenja |
| Sven Gavranović | Dokumentacija |
| Ivan Benčić | Simulacija |
| Domagoj Lepen | Testiranje |

## Što je SpiroBird?

Potenciometar emulira protok daha (ml/s). Korisnik upravlja pticom na ekranu tako da
održava stabilan protok u ciljnoj zoni. Vježba je uspješna ako protok ostane
**neprekidno 5 sekundi** unutar zone:

| Prag | Vrijednost |
|---|---|
| Donja referentna linija | **600 ml/s** |
| Početak ciljne zone | **900 ml/s** |
| Kraj ciljne zone / opasnost | **1200 ml/s** — iznad → trenutni neuspjeh |

## Arhitektura

```txt
┌──────────────────────────┐         ESP-NOW broadcast          ┌──────────────────────────┐
│  CONTROLLER / MASTER     │  ──── SpiroPacket @ 30–50 Hz ────► │  DISPLAY / SLAVE         │
│  ESP32-S3-N16R8 dev kit  │        (FF:FF:FF:FF:FF:FF)         │  ESP32-S3 ES3C28P 2.8"   │
│  pot + motor + RGB LED   │                                    │  240x320 ILI9341V        │
│  logika + Wi-Fi + NVS    │                                    │  samo render igre        │
└──────────┬───────────────┘                                    └──────────────────────────┘
           │ HTTP POST (samo nakon SUCCESS/FAIL)
           ▼
   Node.js backend (lokalno / Render.com) + web dashboard
```

- **Controller (Master)** — jedini izvor istine: čita potenciometar, računa
  protok/stabilnost/volumen, vodi state machine, upravlja RGB LED-icom i vibracijskim
  motorom, sprema high score u NVS, spaja se na Wi-Fi preko captive portala
  (`SpiroBird-Setup`) i šalje rezultate na server.
- **Display (Slave)** — samo prima ESP-NOW pakete i renderira igru. **Ne** spaja se na
  Wi-Fi, **ne** bira mrežu, **ne** šalje na server. Skenira Wi-Fi kanale dok ne primi
  valjane pakete, zatim se zaključa na kanal.
- **Offline-first** — ako Wi-Fi ili server nisu dostupni, igra radi lokalno bez ikakvih
  blokada.

Detalji: [`docs/PLAN.md`](docs/PLAN.md) (cijeli plan),
[`docs/wiring.md`](docs/wiring.md) (ožičenje),
[`docs/protocol.md`](docs/protocol.md) (ESP-NOW protokol).

## Struktura repozitorija

```txt
controller/   PlatformIO projekt — Master firmware (ESP32-S3 dev kit)
display/      PlatformIO projekt — Slave firmware (ES3C28P display board)
server/       Node.js/Express backend + web dashboard (Render-kompatibilan)
sim/          Python simulacija i testovi logike (prije flashanja)
docs/         Dokumentacija: plan, ožičenje, protokol, testiranje, prezentacija
```

## Status implementacije

| Faza | Opis | Status |
|---|---|---|
| 0 | Repozitorij + plan | ✅ |
| 1 | Dokumentacija arhitekture (README, wiring, protocol) | ✅ |
| 2 | Zajednički `protocol.h` | ✅ |
| 3 | Controller firmware — jezgra (senzor, logika, haptika, NVS) | ✅ **validirano na hardveru** |
| 4 | Controller firmware — mreža (ESP-NOW, Wi-Fi portal, server POST) | ✅ **validirano na hardveru** |
| 5 | Display firmware (receiver, render igre, UI ekrani, touch) | ✅ **validirano na hardveru** |
| 6 | Backend (Express + dashboard) | ✅ testirano lokalno (svi endpointi) |
| 7 | Simulacija i testovi (`sim/`) | ✅ 23 unit testa + 5 scenarija — svi prolaze |
| 8 | Finalna dokumentacija (testing-plan, troubleshooting, case, izvještaji) | ✅ |
| 9 | Prezentacija (.pptx u `docs/presentation/`, committano u repo) | ✅ |
| 10 | Backend u produkciji ([spirobird.onrender.com](https://spirobird.onrender.com)) + priprema za obranu | ✅ |

> Hardverska validacija (koraci 1–7 + sleep modovi) provedena na stvarnim uređajima —
> sirovi serijski logovi testova su u [`docs/test-logs/`](docs/test-logs/).

> Projekt pošteno opisuje planiranu/implementiranu arhitekturu — stavke označene ⏳
> još nisu testirane na hardveru.

## Brzi start (nakon implementacije)

```sh
# Controller firmware
cd controller
pio run                      # build
pio run -t upload            # flash
pio device monitor -b 115200 # serial monitor

# Display firmware
cd display
pio run -t upload && pio device monitor -b 115200

# Backend lokalno
cd server
npm install && npm start     # http://localhost:3000

# Simulacija/testovi (prije flashanja!)
cd sim
python test_logic.py
python simulate_breath.py
```

## Obavezni redoslijed testiranja na hardveru

Ne pretpostavljati da sve radi odjednom — strogo ovim redom
(detalji u `docs/testing-plan.md`):

1. **Controller standalone** — Serial: ADC, kalibracija, protok
2. **Buzzer** — eliminiran nakon HW testa (komponenta neispravna); statusna indikacija preuzeta onboard RGB LED-icom
3. **Motor** — tek nakon vizualne provjere tranzistorskog spoja (`ENABLE_MOTOR 0` zadano!)
4. **Backend** — lokalno → curl → ESP32 POST → Render
5. **Display standalone** — fake-data mod bez ESP-NOW-a
6. **ESP-NOW** — broadcast → channel scan → lock → živi protok na ekranu
7. **Puna igra** — uspjeh 5 s, fail > 1200 ml/s, volumen, POST rezultata

## Sigurnosna upozorenja ⚠️

- **Nikad 5 V u 3V3 pin.**
- **Nikad motor direktno na GPIO ili 3V3** — uvijek kroz NPN tranzistor s flyback diodom.
- Motor je nominalno 3 V, na 5 V smije raditi **samo u kratkim pulsevima** (max 250 ms).
- Zajednički GND između ESP32 i vanjskog napajanja motora je **obavezan**.
- Detalji u [`docs/wiring.md`](docs/wiring.md).
