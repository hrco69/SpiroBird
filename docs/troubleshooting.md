# SpiroBird — Troubleshooting vodič

> Svi problemi u ovom vodiču su se **stvarno dogodili** tijekom razvoja i testiranja
> na fizičkom hardveru. Format: simptom → dijagnoza → rješenje.
> Detaljniji kontekst: [`docs/test-izvjestaj.md`](test-izvjestaj.md).

## Serijska komunikacija

### Nema ispisa firmwarea, samo ROM boot poruke
- **Dijagnoza:** ploča ima DVA USB puta — nativni USB CDC (`Serial`) i CH340 UART
  (`Serial0`). Ispis ide na CDC, a monitor je spojen na CH340 port.
- **Rješenje:** u našem firmwareu ispis ide na OBA porta (`DEBUG_MIRROR_UART0 1` u
  `config.h`) — monitor radi na bilo kojem. Alternativno prebaciti kabel na port
  označen "USB".

### `nvs_get_blob ... NOT_FOUND` pri prvom bootu
- **Dijagnoza:** bezopasno — ključ visokog rezultata još ne postoji u NVS-u.
- **Rješenje:** riješeno `isKey()` provjerama u `Storage.cpp`; poruka se više ne javlja.

## Napajanje i resetovi

### Uređaj se nasumično resetira / gasi
- **Dijagnoza:** boot log ispisuje razlog reseta. `POWERON` = fizički prekid napajanja
  (najčešće USB kabel!); `BROWNOUT` = propad napona (kratki spoj, slab izvor);
  `PANIC` = softverski crash; `TASK_WDT/INT_WDT` = blokirana petlja.
- **Naš slučaj:** serija `POWERON` resetova → zamjena USB kabela riješila problem.
- **Savjet:** uvijek prvo pročitati `[boot] reset reason:` liniju.

## Display (LCDWiki ES3C28P)

### Crash pri pokretanju: `StoreProhibited, EXCVADDR 0x00000010` u TFT init-u
- **Dijagnoza:** poznati neriješeni bug **TFT_eSPI** biblioteke na ESP32-S3 s Arduino
  core ≥ 2.0.14 (Bodmer/TFT_eSPI issue #3743). Log u
  [`test-logs/korak5-display-tft-crash.txt`](test-logs/korak5-display-tft-crash.txt).
- **Rješenje:** projekt koristi **LovyanGFX** (`display/include/display_config.h`) —
  ne vraćati TFT_eSPI bez provjere da je bug riješen.

### Inverzne boje (negativ)
- **Rješenje:** `cfg.invert = true` u panel konfiguraciji (potvrđeno za naš primjerak).

### Zamijenjene crvena/plava
- **Rješenje:** `cfg.rgb_order = true`.

### Slika zrcalna ili rotirana
- **Rješenje:** `DISPLAY_ROTATION` u `display/include/config.h` (1 ↔ 3 za landscape).

### Bijeli/crni ekran
- **Provjeriti redom:** backlight pin (45) svijetli? → SPI pinovi (CS=10, DC=46,
  SCLK=12, MOSI=11) prema LCDWiki dokumentaciji → spustiti `freq_write` na 27 MHz.

### Touch ne radi
- **Dijagnoza:** boot log ispisuje `[gfx] touch (FT6336G) ready` ili `NOT detected`.
- **Provjeriti:** TP reset pin (18) se otpušta prije inita; I2C pinovi SDA=16, SCL=15;
  adresa 0x38. Bez toucha display i dalje radi — budi se na aktivnost igre.

## ESP-NOW

### Display ne nalazi signal / "SCANNING CH n" u krug
- **Provjeriti redom:**
  1. Controller boot log: `[espnow] ready ... channel N` — šalje li uopće?
  2. `sizeof(SpiroPacket)=48` na OBA uređaja (mora biti identičan!)
  3. Display log: raste li `bad` brojač? → verzija protokola se razlikuje, reflashati oba
- **Napomena:** s Wi-Fi-jem spojen Controller šalje na kanalu rutera; offline na
  fiksnom kanalu 1.

### Display naizmjence hvata i gubi signal (svake ~sekunde)
- **Dijagnoza (naš slučaj):** Wi-Fi stack auto-reconnect na Controlleru neprestano
  skenira kanale tražeći nedostupan ruter i vuče ESP-NOW TX kanal sa sobom.
- **Rješenje (ugrađeno):** auto-reconnect je isključen; offline je terminalno stanje s
  parkiranim kanalom; runtime reconnect je ograničen (2 pokušaja pa offline). Ako se
  simptom ikad vrati, prvi sumnjivac je novi kod koji dira Wi-Fi konfiguraciju.

## Wi-Fi provisioning

### Portal se ne pojavljuje
- **Po dizajnu:** portal se otvara SAMO ako nema spremljenih kredencijala, ili
  **držanjem gumba (GPIO21) tijekom uključivanja**. Ako spremljena mreža postoji ali
  je nedostupna → decision ekran na displayu (kratki pritisak = offline, dugi = portal).

### "Spojio sam se na SpiroBird-Setup ali se stranica ne otvara"
- Ručno otvoriti `http://192.168.4.1`. Isključiti mobilne podatke na telefonu.

### ESP32 ne može do lokalnog servera
- Windows firewall: dopustiti inbound TCP 3000. ESP32 i laptop na ISTOJ mreži
  (eduroam tipično izolira klijente — koristiti hotspot).

## Potenciometar / ADC

### Protok "zaglavljen" na 1400 ml/s
- **Dijagnoza:** offset kalibriran dok je knob bio na krajnjem položaju.
- **Rješenje (ugrađeno):** kalibracija čeka knob u zoni 1700–2300 držan 2 s; display
  navodi korisnika (turn UP/DOWN).

### Uređaj ne ide u sleep
- **Dijagnoza:** nešto stalno javlja aktivnost. Serial sad ispisuje
  `[sleep] activity: <izvor>` — izvor imenuje krivca.
- **Naš slučaj:** "scratchy" mjesto na potenciometru → ADC šiljci → riješeno
  debounceom pomaka (50 ms kontinuirano).

## Vibracijski motor

### Motor se vrti neprekidno od uključivanja
- **Hardverska provjera:** držati RST — ako se motor zaustavi, pull-down i tranzistor
  rade, problem je softverski. Ako se NE zaustavi: tranzistor pogrešno okrenut
  (PN2222A je E-B-C, BC337 je C-B-E — obrnuto!) ili motor(−) spojen na GND umjesto
  na kolektor.
- **Naš slučaj (softverski):** asinkroni puls pokrenut prije blokirajućeg Wi-Fi
  portala — watchdog u `loop()` nije se izvršavao. Riješeno sinkronim boot pulsom.
  **Pouka: sigurnosna logika ne smije ovisiti o glavnoj petlji.**

### Vibracija se jedva osjeti
- Veliki motor s teškim ekscentrom treba ~300 ms zaleta — pulsevi su podesivi u
  `config.h` (`MOTOR_PULSE_*`, FAIL je uzorak 3 × 600 ms).

## Buzzer (eliminiran)

- Pasivni piezo na DC samo klikće — mora PWM/LEDC. Najglasniji je na rezonanciji
  (2–4 kHz); ispod ~1 kHz jedva čujan. Naš primjerak ni nakon prilagodbe frekvencija
  nije proizvodio ton → eliminiran (`ENABLE_BUZZER 0`), kod ostaje u repou iza
  zastavice za slučaj zamjene komponente.

## Upload / flashanje

### Upload ne uspijeva
1. Držati **BOOT**, kratko **RST**, pustiti BOOT → download mode → ponoviti upload
2. Nakon uploada jednom pritisnuti RST
3. Provjeriti port: `pio device list` (dva porta po ploči!)

### Backend na Renderu "ne radi" prvi zahtjev
- Free tier spava nakon ~15 min → prvi zahtjev traje 30–60 s, a Controller ima
  timeout 1500 ms → prvi POST padne, retry prolazi. Za demo: otvoriti dashboard par
  minuta ranije da se servis probudi.