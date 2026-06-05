# SpiroBird — ESP-NOW protokol

> Izvorna definicija: `controller/include/protocol.h` i `display/include/protocol.h`
> — **datoteke moraju biti bajt-identične**. Svaka izmjena strukture zahtijeva kopiranje
> u oba projekta i povećanje `SPIROBIRD_PROTOCOL_VERSION`.

## Osnovne konstante

| Konstanta | Vrijednost | Značenje |
|---|---|---|
| `SPIROBIRD_MAGIC` | `0x5342` ("SB") | identifikacija SpiroBird paketa |
| `SPIROBIRD_PROTOCOL_VERSION` | `1` | verzija strukture paketa |
| Odredišna MAC adresa | `FF:FF:FF:FF:FF:FF` | broadcast — bez hardkodiranih MAC-ova |
| Frekvencija slanja | 30–50 Hz | samo Controller → Display |
| Veličina paketa | **48 bajtova** | daleko ispod ESP-NOW limita od 250 B |

## Smjer komunikacije

Komunikacija je **jednosmjerna**: Controller šalje, Display samo prima.
Display nikad ne šalje pakete natrag (nema ACK na aplikacijskoj razini — gubitak
pojedinačnih paketa je prihvatljiv jer sljedeći paket stiže za 20–33 ms).

## Struktura `SpiroPacket` (packed, 48 B)

| Offset | Polje | Tip | Veličina | Opis |
|---|---|---|---|---|
| 0 | `magic` | `uint16_t` | 2 | `0x5342` |
| 2 | `version` | `uint8_t` | 1 | verzija protokola |
| 3 | `seq` | `uint32_t` | 4 | brojač paketa (detekcija gubitka/restarta) |
| 7 | `timestampMs` | `uint32_t` | 4 | `millis()` Controllera |
| 11 | `rawAdc` | `uint16_t` | 2 | sirovi ADC 0–4095 |
| 13 | `deviationAdc` | `int16_t` | 2 | `rawAdc − kalibrirani offset` |
| 15 | `flowMlS` | `float` | 4 | mapirani protok prije filtriranja |
| 19 | `filteredFlowMlS` | `float` | 4 | EMA-filtrirani protok (pozicija ptice) |
| 23 | `volumeMl` | `float` | 4 | integrirani volumen pokušaja |
| 27 | `maxFlowMlS` | `float` | 4 | maksimalni protok pokušaja |
| 31 | `avgFlowMlS` | `float` | 4 | prosječni protok pokušaja |
| 35 | `stableTimeMs` | `uint16_t` | 2 | neprekidno stabilno vrijeme 0–5000 |
| 37 | `state` | `uint8_t` | 1 | `ExerciseState` |
| 38 | `failReason` | `uint8_t` | 1 | `FailReason` |
| 39 | `targetZone` | `bool` | 1 | 900 ≤ filtrirano ≤ 1200 ml/s |
| 40 | `dangerZone` | `bool` | 1 | filtrirano > 1200 ml/s |
| 41 | `success` | `bool` | 1 | pokušaj uspješan |
| 42 | `fail` | `bool` | 1 | pokušaj neuspješan |
| 43 | `deepSleepPending` | `bool` | 1 | Controller upravo ulazi u deep sleep |
| 44 | `wifiStatus` | `uint8_t` | 1 | `WifiStatus` |
| 45 | `serverStatus` | `uint8_t` | 1 | `ServerStatus` |
| 46 | `espNowChannel` | `uint8_t` | 1 | kanal na kojem Controller šalje |
| 47 | `checksum` | `uint8_t` | 1 | XOR bajtova 0–46 — **uvijek zadnji** |

`static_assert` u `protocol.h` jamči veličinu od 48 B i poziciju checksuma — promjena
rasporeda ruši kompilaciju umjesto da tiho pokvari komunikaciju.

> Polje `deepSleepPending` je dodano u odnosu na prvotnu skicu strukture (v1) kako bi
> Display mogao prikazati sleep ekran *prije* nego Controller ugasi radio.

## Enumeracije

### `ExerciseState`

| Vrijednost | Stanje | Display prikazuje |
|---|---|---|
| 0 | `STATE_IDLE` | idle ekran / "pritisni gumb" |
| 1 | `STATE_CALIBRATING` | "Kalibracija — ne diraj potenciometar" |
| 2 | `STATE_READY` | "Počni puhati!" |
| 3 | `STATE_ACTIVE` | igra (ptica, zone, HUD) |
| 4 | `STATE_SUCCESS` | success ekran + rezultat |
| 5 | `STATE_FAIL` | fail ekran + razlog |
| 6 | `STATE_RESULT` | sažetak rezultata |
| 7 | `STATE_SLEEP` | "Controller sleeping / Press wake switch" |

### `FailReason`

`FAIL_NONE=0`, `FAIL_OVER_1200=1`, `FAIL_UNSTABLE=2`, `FAIL_COLLISION=3`, `FAIL_TIMEOUT=4`

### `WifiStatus` (Display ovo samo renderira — nikad se sam ne spaja!)

| Vrijednost | Status | Display prikazuje |
|---|---|---|
| 0 | `WIFI_ST_DISABLED` | ništa / "Wi-Fi off" |
| 1 | `WIFI_ST_CONNECTING` | "Spajanje na Wi-Fi…" |
| 2 | `WIFI_ST_SETUP_PORTAL` | "Wi-Fi Setup: Connect to SpiroBird-Setup / Open 192.168.4.1 / Or press button to skip" |
| 3 | `WIFI_ST_CONNECTED` | "Wi-Fi connected" |
| 4 | `WIFI_ST_OFFLINE` | "Playing Offline / Local Mode" |

### `ServerStatus`

`SERVER_ST_UNKNOWN=0`, `SERVER_ST_ONLINE=1`, `SERVER_ST_OFFLINE=2`, `SERVER_ST_DISABLED=3`

## Checksum

```cpp
uint8_t x = 0;
for (i = 0; i < sizeof(SpiroPacket) - 1; i++) x ^= bytes[i];
```

- Controller: `spiroPacketFinalize()` upisuje magic, verziju i checksum prije slanja
- Display: `spiroPacketValid(data, len)` odbacuje paket ako je kriva veličina, magic,
  verzija ili checksum — nevaljani paketi se broje i ispisuju na Serial, ali se
  **nikad ne renderiraju**

## Usklađivanje Wi-Fi i ESP-NOW kanala

Problem: kad se Controller spoji na kućni Wi-Fi, radi na kanalu te mreže. ESP-NOW mora
koristiti **isti kanal** da bi Display primao pakete.

### Controller (Master)

1. Spoji se na Wi-Fi (ako je `ENABLE_WIFI 1`)
2. Očita aktivni kanal: `esp_wifi_get_channel()`
3. Inicijalizira ESP-NOW na tom kanalu
4. Šalje pakete na broadcast MAC; **kanal je upisan u svaki paket** (`espNowChannel`)
5. Ako je Wi-Fi isključen/offline → fiksni kanal iz `config.h` (zadano 1)

### Display (Slave) — channel scan / lock / re-scan

```txt
SCANNING:  kanal 1 → 2 → … → 13 → 1 …  (zadržavanje ~300 ms po kanalu)
           Serial/ekran: "NO SIGNAL / SCANNING CH n"
LOCK:      nakon ≥ 3 valjana paketa na istom kanalu → zaključaj kanal
           Serial/ekran: "LOCKED CH n / RECEIVING SPIROBIRD DATA"
TIMEOUT:   > 1000 ms bez paketa → ekran "NO SIGNAL" (zadnje stanje se zamrzava,
           stale podaci se NE koriste za render)
RE-SCAN:   > 3000 ms bez paketa → natrag u SCANNING
```

Display se **ne** spaja na Wi-Fi, ne koristi WiFiManager i ne sprema vjerodajnice —
samo `WiFi.mode(WIFI_STA)` + `esp_wifi_set_channel()` za skeniranje.

## Watchdog-style pravila (obje strane)

- Nevaljani paketi se ignoriraju (broje se za dijagnostiku)
- Stale podaci (> 1000 ms) se ne renderiraju kao živi
- `seq` skok unatrag = restart Controllera → Display resetira statistiku
- Gubitak signala nikad ne ruši ni jednu stranu — UI prikazuje status i čeka

## Fallback (samo ako ESP-NOW bude nestabilan)

Dokumentirana rezerva, **ne implementira se u v1**: oba uređaja na istom Wi-Fi-ju,
Controller šalje iste `SpiroPacket` strukture kao UDP broadcast na port iz `config.h`.
ESP-NOW ostaje primarni mehanizam.
