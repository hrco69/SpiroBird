# SpiroBird — Izvještaj o testiranju na fizičkom hardveru

> **Zadužen za testiranje:** Domagoj Lepen
> **Razdoblje testiranja:** lipanj 2026.
> **Referentni plan:** [`docs/testing-plan.md`](testing-plan.md)
> **Sirovi serijski logovi:** [`docs/test-logs/`](test-logs/)

## 1. Sažetak

Testiranje je provedeno po obaveznom bring-up redoslijedu iz plana testiranja
(koraci 1–7 + power management). Tijekom testiranja otkriveno je 8 značajnih
problema — svi su dijagnosticirani iz serijskih logova, ispravljeni i ponovno
testirani. Konačno stanje: svi koraci prolaze.

## 2. Rezultati po koracima

| Korak | Test | Rezultat |
|---|---|---|
| 1 | Controller standalone (ADC, kalibracija, protok) | PROLAZI (nakon bugova #1, #4) |
| 2 | Buzzer | ELIMINIRAN (bug #5) — indikaciju preuzela RGB LED |
| 3 | Vibracijski motor (NPN driver) | PROLAZI (nakon bugova #6, #7) |
| 4 | Backend lokalno + Wi-Fi portal + POST | PROLAZI |
| 5 | Display standalone (driver, orijentacija, demo) | PROLAZI (nakon bugova #2, #3) |
| 6 | ESP-NOW (scan/lock/re-lock) | PROLAZI (nakon buga #8) |
| 7 | Puna igra (uspjeh 5 s, fail >1200, volumen, POST) | PROLAZI |
| PM | Sleep modovi (pseudo/deep + touch buđenje displaya) | PROLAZI |

## 3. Pronađeni problemi (kronološki)

### #1 — Serijski ispis se ne vidi
- **Simptom:** nakon flashanja na monitoru samo ROM boot poruke, bez ispisa firmwarea.
- **Uzrok:** ploča ima dva USB puta — naš ispis ide na nativni USB CDC, monitor je bio
  na CH340 UART portu.
- **Rješenje:** debug ispis zrcaljen na OBA porta (`DEBUG_MIRROR_UART0`).

### #2 — Display se ruši pri pokretanju (Guru Meditation)
- **Simptom:** `StoreProhibited, EXCVADDR 0x10` u TFT init-u, beskonačni crash-loop.
  Log: [`test-logs/korak5-display-tft-crash.txt`](test-logs/korak5-display-tft-crash.txt)
- **Uzrok:** poznati neriješeni bug TFT_eSPI biblioteke na ESP32-S3 s Arduino core
  ≥ 2.0.14 (Bodmer/TFT_eSPI issue #3743).
- **Rješenje:** migracija na LovyanGFX (predviđeni fallback iz plana rizika).

### #3 — Inverzne boje na ekranu
- **Simptom:** nebo svijetlo, "zeleni" koridor cijan.
- **Uzrok:** ILI9341V panel na ovoj ploči zahtijeva inverziju.
- **Rješenje:** `cfg.invert = true` u konfiguraciji panela.

### #4 — Kriva kalibracija i nasumični resetovi
- **Simptom:** offset kalibriran na ADC railu (3900), pa konstantan protok 1400 ml/s;
  povremeni potpuni resetovi uređaja.
  Log: [`test-logs/korak2-controller-kalibracija.txt`](test-logs/korak2-controller-kalibracija.txt)
- **Uzrok:** kalibracija prosjekom u proizvoljnom položaju knoba; resetovi su bili
  `POWERON` tip = fizički prekid USB napajanja (kabel), potvrđeno ispisom razloga reseta.
- **Rješenje:** redizajn kalibracije — fiksni centar 2000, čeka se knob u zoni
  1700–2300 držan 2 s (display navodi korisnika: "turn UP/DOWN"); zamijenjen kabel.

### #5 — Buzzer proizvodi samo "tik"
- **Simptom:** bez tona, samo klik pri resetu, i nakon prilagodbe frekvencija na
  rezonantni pojas pieza (2–3 kHz) i boot self-test melodije.
- **Odluka:** komponenta neispravna/neprikladna → eliminirana iz projekta
  (`ENABLE_BUZZER 0`); statusna indikacija prebačena na onboard RGB LED.

### #6 — KRITIČNO: motor se vrti neprekidno od boota
- **Simptom:** motor radi kontinuirano; RST ga zaustavi, puštanjem opet kreće.
- **Dijagnostika:** hardver dokazano ispravan (RST → pull-down drži tranzistor OFF);
  multimetrom potvrđeno ožičenje.
- **Uzrok:** softverski — asinkroni boot self-test puls uključio je pin, a watchdog
  koji ga gasi živi u `loop()` koji se ne izvršava dok `setup()` blokira na Wi-Fi
  portalu (do 180 s).
- **Rješenje:** boot self-test postao sinkroni (HIGH → delay → LOW prije bilo kakvog
  blokiranja) + dodatni watchdog tick nakon blokirajućeg HTTP POST-a.
- **Pouka:** sigurnosni mehanizam ne smije ovisiti o tome da se glavna petlja vrti.

### #7 — Haptika neosjetna
- **Simptom:** pulsevi od 80–250 ms jedva se osjete.
- **Uzrok:** ugrađen je veliki vibro motor s teškim ekscentrom — treba ~300 ms zaleta.
- **Rješenje:** produženi pulsevi (300/500 ms) + FAIL kao uzorak 3 × 600 ms; tvrdi
  limit po segmentu (1000 ms) i cooldown zadržani.

### #8 — ESP-NOW "nasumično mrtav", display gubi signal u petlji
- **Simptom:** display svake sekunde uhvati pa izgubi signal; pomaže tek više resetova
  controllera. Pojavilo se tek nakon uključenja Wi-Fi-ja.
- **Uzrok:** dva sloja — (a) tranzijentni neuspjeh spajanja otvarao je portal od 180 s
  na kanalu 1; (b) ugrađeni auto-reconnect Wi-Fi stacka stalno skenira kanale i vuče
  ESP-NOW TX kanal sa sobom.
- **Rješenje:** deterministički workflow — portal samo bez spremljenih kredencijala ili
  gumbom; auto-reconnect isključen; offline je terminalno stanje s parkiranim kanalom;
  runtime gubitak veze = ograničeni broj pokušaja pa offline; decision ekran
  (kratki/dugi pritisak) za slučaj nedostupne spremljene mreže.

## 4. Statistika igranja (sa backend servera)

Tijekom završnog testiranja (korak 7) odigrano je **29 pokušaja: 19 uspješnih (66 %),
10 neuspješnih** — svi neuspjesi s razlogom `FAIL_OVER_1200`, što potvrđuje da je
težina igre dobro balansirana (igrač griješi probijanjem gornje granice, ne
nestabilnošću). Najbolji volumen: **17.5 L**; tipičan uspješan pokušaj: 6–7 L.

## 5. Validacija power managementa

- Controller: pseudo sleep nakon 60 s neaktivnosti iz bilo kojeg stanja; deep sleep
  nakon 3 min; buđenje gumbom (EXT0), pomakom potenciometra i hladnim bootom —
  ispis `wake reason: EXT0` potvrđen.
- Display: dim nakon 1 min, backlight off nakon dodatnih 15 s; buđenje dodirom
  (FT6336G) i automatski na aktivnost igre; ESP-NOW prijem radi u svim stanjima.

## 6. Zaključak

Sustav je spreman za demonstraciju uživo. Svi rizici iz plana (display driver,
ESP-NOW kanali, motor, Wi-Fi blokiranje) su se materijalizirali tijekom testiranja i
svi su riješeni uz dokumentirane uzroke — detalji za rješavanje sličnih problema su u
[`docs/troubleshooting.md`](troubleshooting.md).
