# SpiroBird – IoT digitalni spirometar u obliku igre

> SpiroBird je projekt IoT digitalnog spirometra razvijen u sklopu kolegija **Razvoj ugradbenih sustava**. Sustav koristi fizički hardware, dva ESP32-S3 mikrokontrolera, potenciometar kao emulator protoka daha, 2.8" ESP32-S3 zaslon, buzzer, vibro motor i planiranu IoT komunikaciju s poslužiteljem. Umjesto klasičnog prikaza kuglica, korisničko sučelje je zamišljeno kao igra inspirirana Flappy Bird konceptom.

## Opis projekta

Ovaj projekt je rezultat timskog rada u sklopu projektnog zadatka kolegija **Razvoj ugradbenih sustava** na Tehničkom veleučilištu u Zagrebu.

Klasični poticajni spirometar koristi se za vježbanje i rehabilitaciju disanja. U ovom projektu stvarni senzor protoka zraka zamijenjen je potenciometrom, koji služi kao emulator protoka daha. Pomicanjem potenciometra simulira se jačina udaha/izdaha, a mikrokontroler tu vrijednost pretvara u protok izražen u ml/s.

Projekt je zamišljen kao fizički uređaj, a ne samo simulacija. Koristit će se dva ESP32-S3 uređaja:

- jedan ESP32-S3 razvojni modul služi kao upravljačka jedinica za očitanje potenciometra, obradu signala, buzzer, vibro motor, Wi-Fi i slanje rezultata
- drugi ESP32-S3 uređaj s 2.8" zaslonom služi za prikaz igre, protoka, stabilnosti i rezultata korisnika

Vizualni scenarij projekta zove se **SpiroBird**. Snaga daha održava visinu ptice na ekranu. Cilj korisnika je održavati stabilan protok u ciljnoj zoni. Ako je protok prenizak, ptica pada. Ako je protok stabilan, ptica leti kroz sigurnu zonu. Ako protok prijeđe sigurnosnu granicu od 1200 ml/s, pokušaj se poništava.

Ova verzija README dokumenta opisuje planiranu i implementacijsku arhitekturu projekta. Tijekom razvoja dokument će se ažurirati stvarnim rezultatima, problemima i promjenama koje nastanu tijekom rada na fizičkom hardwareu.

## Cilj projekta

Cilj projekta je razviti interaktivni IoT spirometar koji:

- mjeri simulirani protok daha pomoću potenciometra
- obrađuje signal u stvarnom vremenu
- prikazuje stanje vježbe kroz igru
- korisniku daje vizualni, zvučni i taktilni feedback
- izračunava rezultat vježbe
- sprema ili šalje rezultat na poslužitelj
- demonstrira primjenu prekida, neblokirajućeg programiranja i upravljanja potrošnjom energije

## Funkcijski zahtjevi

Sustav mora omogućiti:

- periodično očitavanje vrijednosti potenciometra
- pretvorbu vrijednosti potenciometra u simulirani protok daha
- filtriranje ulaznog signala radi smanjenja naglih promjena
- prikaz trenutnog protoka na zaslonu
- prikaz tri razine protoka:
  - 600 ml/s
  - 900 ml/s
  - 1200 ml/s
- prikaz indikatora stabilnosti protoka
- praćenje trajanja stabilnog udaha/izdaha
- detekciju uspješne vježbe ako je protok stabilan najmanje 5 sekundi
- poništavanje pokušaja ako protok prijeđe 1200 ml/s
- izračun ukupnog volumena daha numeričkom integracijom protoka kroz vrijeme
- prikaz rezultata korisnika nakon završetka pokušaja
- slanje rezultata poslužitelju putem Wi-Fi veze
- lokalno spremanje najboljeg rezultata u trajnu memoriju
- zvučni feedback pomoću pasivnog buzzera
- taktilni feedback pomoću vibro motora
- smještaj elektronike u 3D printano kućište

## Uvjet uspješne vježbe

Vježba je uspješna ako su istovremeno zadovoljeni sljedeći uvjeti:

- protok je najmanje 900 ml/s
- protok ne prelazi 1200 ml/s
- signal ostaje stabilan unutar zadane ciljne zone
- uvjet traje neprekidno najmanje 5 sekundi

Ako protok prijeđe 1200 ml/s, pokušaj se poništava i korisnik mora započeti novu vježbu.

## Koncept igre

SpiroBird koristi logiku sličnu Flappy Bird igri, ali se ptica ne upravlja tipkom, nego simuliranim protokom daha.

Osnovna pravila igre:

- prenizak protok: ptica gubi visinu
- protok oko 600 ml/s: ptica se počinje podizati, ali nije u idealnoj zoni
- protok između 900 i 1200 ml/s: ptica leti stabilno kroz ciljnu zonu
- protok iznad 1200 ml/s: pokušaj se poništava
- stabilan protok 5 sekundi: vježba je uspješna

Cilj nije samo postići veliki protok, nego održati stabilan i kontroliran protok. Time se zadržava medicinski smisao zadatka, ali se prikazuje kroz zabavno korisničko sučelje.

## Arhitektura sustava

Sustav se sastoji od dvije glavne fizičke cjeline.

```text
+-----------------------------+        ESP-NOW        +------------------------------+
| Controller ESP32-S3         |  ------------------>  | Display ESP32-S3 ES3C28P     |
|                             |                       |                              |
| - potenciometar             |                       | - prikaz igre                |
| - obrada signala            |                       | - prikaz protoka             |
| - logika vježbe             |                       | - stability indikator        |
| - buzzer                    |                       | - rezultat pokušaja          |
| - vibro motor               |                       | - status konekcije           |
| - NVS high score            |                       |                              |
| - Wi-Fi prema serveru       |                       |                              |
+-----------------------------+                       +------------------------------+
              |
              | Wi-Fi / HTTP
              v
+-----------------------------+
| Node.js server / Render.com |
|                             |
| - spremanje rezultata       |
| - dohvat najboljeg rezultata|
| - pregled povijesti         |
+-----------------------------+
```

### Controller ESP32-S3

Controller ESP32-S3 je glavna upravljačka jedinica sustava. Na njega se spajaju komponente koje zahtijevaju klasične GPIO pinove.

Zadaci controller modula:

- očitavanje potenciometra
- filtriranje signala
- pretvorba ADC vrijednosti u protok
- izračun volumena
- praćenje stabilnosti
- detekcija uspjeha i neuspjeha
- upravljanje buzzerom
- upravljanje vibro motorom preko tranzistora
- spremanje najboljeg rezultata u NVS memoriju
- slanje rezultata na poslužitelj
- slanje stanja igre display modulu putem ESP-NOW komunikacije

### Display ESP32-S3 ES3C28P

Display modul služi za prikaz korisničkog sučelja. Budući da ES3C28P nema klasične pinove za jednostavno spajanje dodatnih komponenti, koristi se primarno za prikaz igre i statusa.

Zadaci display modula:

- primanje podataka s controller ESP32-a
- prikaz SpiroBird igre
- prikaz trenutnog protoka
- prikaz pragova 600, 900 i 1200 ml/s
- prikaz indikatora stabilnosti
- prikaz trenutnog volumena
- prikaz uspjeha ili greške
- prikaz najboljeg rezultata

## Komponente

Planirane komponente:

| Komponenta | Namjena |
|---|---|
| ESP32-S3-N16R8 razvojni modul | Controller modul za očitanje, obradu, buzzer, vibro motor i Wi-Fi |
| 2.8 inch ESP32-S3 Display ES3C28P | Display modul za grafički prikaz igre |
| Potenciometar B10K WL | Emulator protoka daha |
| Pasivni buzzer | Zvučni feedback korisniku |
| Vibro motor 3 V | Taktilni feedback kod greške, sudara ili izlaska iz zone |
| NPN tranzistor 2N2222 / 2N2222A / PN2222A ili BC337 | Uključivanje vibro motora bez opterećenja ESP32 GPIO pina |
| Flyback dioda 1N4007 ili 1N4148 | Zaštita od povratnog napona motora |
| Otpornik 1 kΩ | Ograničenje struje baze tranzistora |
| Otpornik 330 Ω | Alternativa ako motor nema dovoljno snage preko 1 kΩ |
| Zasebno napajanje za motor | Stabilnije napajanje motora i zaštita mikrokontrolera |
| 3D printano kućište | Fizička zaštita i uredan smještaj elektronike |

## Planirano spajanje komponenti

Točan pinout može se prilagoditi tijekom testiranja, ali početni prijedlog spajanja je:

| Komponenta | ESP32-S3 pin | Napomena |
|---|---:|---|
| Srednji pin potenciometra | GPIO4 / ADC | Analogno očitanje |
| Vanjski pin potenciometra | 3V3 | Napajanje potenciometra |
| Drugi vanjski pin potenciometra | GND | Masa |
| Pasivni buzzer | GPIO5 | Upravljanje PWM signalom preko LEDC funkcije |
| Baza NPN tranzistora za motor | GPIO6 preko 1 kΩ | GPIO ne smije direktno napajati motor |
| Vibro motor | zasebno napajanje + tranzistor | Kratki impulsi tijekom greške ili sudara |
| Tipka za start/reset | GPIO7 | Po potrebi za prekid i buđenje iz sleep moda |
| GND ESP32-a i zasebnog napajanja | zajednički GND | Obavezno povezati mase |

Napomena: pinove koji se koriste za boot, USB ili posebne funkcije treba izbjegavati ako stvaraju probleme pri programiranju ili pokretanju pločice.

## Spajanje vibro motora

Vibro motor se neće spajati direktno na ESP32 GPIO pin jer motor može povući veću struju od dopuštene za GPIO i može generirati povratni napon pri isključivanju.

Planirano spajanje:

```text
+5V ili zasebno napajanje
        |
        |
      Motor
        |
        +------|<|------+
        |    Flyback    |
        |     dioda     |
        |
     Kolektor
      NPN tranzistor
     Emiter
        |
       GND

ESP32 GPIO ---- otpornik 1 kΩ ---- baza tranzistora
```

Dioda se spaja paralelno s motorom. Katoda diode ide na pozitivni pol napajanja motora, a anoda na stranu motora koja ide prema tranzistoru.

Motor je deklariran kao 3 V, ali je testiran i na 5 V. U konačnoj izvedbi koristit će se kratki impulsi, a po potrebi će se jačina vibracije ograničiti PWM signalom ili kraćim trajanjem impulsa.

## Pasivni buzzer

Buzzer je pasivni, što znači da se ne aktivira samo stalnim HIGH signalom. Za stvaranje tona koristit će se PWM signal preko ESP32 LEDC funkcionalnosti.

Planirani zvučni signali:

| Događaj | Signal |
|---|---|
| Početak pokušaja | kratki beep |
| Ulazak u ciljnu zonu | kratki potvrđujući beep |
| Uspješna vježba | dulji pozitivan ton |
| Greška / protok iznad 1200 ml/s | niski error ton |
| Reset pokušaja | kratki dvostruki beep |

## Obrada signala

### Očitavanje potenciometra

Potenciometar se očitava periodički, bez korištenja blokirajuće funkcije `delay()`. Očitavanje će se izvoditi u pravilnim vremenskim intervalima pomoću timer logike i/ili `millis()` pristupa.

Planirani interval očitanja:

```text
10 ms do 20 ms
```

To daje dovoljno brz odziv za igru, ali ne opterećuje mikrokontroler nepotrebno.

### Kalibracija nulte točke

Na početku rada sustav može izmjeriti početnu vrijednost potenciometra i koristiti je kao offset. Time se izbjegava problem ako potenciometar nije fizički točno u sredini.

```text
offset = prosjek nekoliko početnih ADC očitanja
```

### Pretvorba ADC vrijednosti u protok

ESP32 koristi ADC očitanje koje se zatim pretvara u simulirani protok daha.

Primjer logike:

```text
deviation = abs(adcValue - offset)
flowMlS = map(deviation, 0..maxDeviation, 0..1400)
```

Vrijednost se zatim ograničava na očekivani raspon.

### Filtriranje signala

Za zaglađivanje signala koristit će se eksponencijalni klizni prosjek, odnosno EMA filter.

```cpp
filteredFlow = alpha * rawFlow + (1.0 - alpha) * previousFilteredFlow;
```

Početna vrijednost koeficijenta:

```text
alpha = 0.20
```

Ova vrijednost je kompromis između stabilnog prikaza i dovoljno brzog odziva igre. Tijekom testiranja na fizičkom hardwareu koeficijent se može promijeniti.

## Izračun volumena

Ukupni volumen daha računa se numeričkom integracijom protoka kroz vrijeme.

```cpp
volumeMl += filteredFlowMlS * deltaTimeSeconds;
```

Primjer:

```text
1000 ml/s kroz 5 s = 5000 ml
```

Na taj način sustav osim trenutnog protoka može prikazati i ukupni volumen vježbe.

## Detekcija stabilnosti

Signal se smatra stabilnim ako:

- protok je unutar ciljane zone
- protok ne prelazi sigurnosnu granicu
- promjena protoka nije prevelika u kratkom vremenu
- uvjet traje kontinuirano najmanje 5 sekundi

Planirana ciljna zona:

```text
900 ml/s <= protok <= 1200 ml/s
```

Prevelike nagle promjene mogu se tretirati kao trzanje potenciometrom ili nerealna vrijednost. Takve promjene sustav može ignorirati ili označiti kao nestabilan protok.

## State machine

Program će biti organiziran kao stroj stanja.

```text
IDLE
  -> čeka korisnika ili start

CALIBRATION
  -> mjeri nultu točku potenciometra

READY
  -> korisniku prikazuje da može započeti vježbu

ACTIVE
  -> očitava signal, računa protok, volumen i stabilnost

SUCCESS
  -> uvjet je bio stabilan 5 sekundi

FAIL
  -> protok je prešao 1200 ml/s ili je detektirana greška

RESULT
  -> prikaz rezultata i slanje na server

SLEEP
  -> sustav ide u niskopotrošni način rada nakon neaktivnosti
```

Ovakva organizacija olakšava modularnost, testiranje i dokumentiranje rada sustava.

## Komunikacija između dva ESP32 uređaja

Za komunikaciju između controller i display ESP32 uređaja planira se koristiti **ESP-NOW**. Taj protokol omogućuje direktnu komunikaciju između ESP uređaja bez potrebe da su oba uređaja spojena na isti router za samu međusobnu komunikaciju.

Controller ESP32 šalje display modulu paket s trenutnim stanjem vježbe.

Primjer planirane strukture paketa:

```cpp
typedef struct {
    uint16_t rawAdc;
    float rawFlowMlS;
    float filteredFlowMlS;
    float volumeMl;
    uint16_t stableTimeMs;
    uint8_t state;
    bool success;
    bool fail;
    uint8_t failReason;
} SpiroPacket;
```

Display ESP32 prima paket i prema njemu ažurira prikaz igre.

Prednost ovog pristupa je što display ne mora raditi obradu signala, nego samo prikazivati stanje koje mu šalje controller.

## IoT povezivost i server

Sustav će nakon završetka pokušaja slati rezultat na poslužitelj putem Wi-Fi veze.

Planirani pristup:

- Node.js server
- Express API
- deployment lokalno ili na Render.com
- spremanje rezultata u JSON datoteku ili jednostavnu bazu
- dohvat najboljeg rezultata

Planirani API endpointi:

```text
POST /api/results
GET  /api/highscore
GET  /api/results
```

Primjer JSON poruke:

```json
{
  "deviceId": "spirobird-01",
  "success": true,
  "volumeMl": 5120,
  "maxFlowMlS": 1080,
  "avgFlowMlS": 965,
  "stableTimeMs": 5000,
  "failReason": null
}
```

U slučaju da server nije dostupan, osnovni rezultat i najbolji rezultat i dalje se mogu spremiti lokalno u ESP32 NVS memoriju.

## Spremanje najboljeg rezultata

Najbolji rezultat spremat će se u trajnu memoriju ESP32 mikrokontrolera pomoću NVS/Preferences mehanizma.

Planirano spremanje:

- najveći volumen
- najdulje stabilno vrijeme
- najveći uspješni score
- broj uspješnih pokušaja

## Upravljanje potrošnjom energije

Projekt će uključiti osnovnu logiku upravljanja potrošnjom energije, povezanu s gradivom laboratorijske vježbe o sleep modovima.

Planirana logika:

- tijekom igre sustav radi aktivno
- nakon dulje neaktivnosti controller prelazi u sleep stanje
- prije sleep stanja gase se nepotrebne periferije
- stanje se po potrebi sprema u memoriju
- buđenje je moguće preko tipke ili timer wake-up mehanizma

Sleep mode neće se koristiti tijekom aktivne igre kako ne bi narušio odziv sustava.

## Primjena prekida i neblokirajućeg programiranja

Program neće koristiti `delay()` u glavnoj logici rada. Umjesto toga koristit će se:

- `millis()` za vremensko upravljanje
- timer logika za periodičko očitanje
- prekid za tipku start/reset
- kratke ISR funkcije koje samo postavljaju zastavice
- obrada događaja u glavnoj petlji programa

Primjer logike:

```cpp
volatile bool sampleFlag = false;
volatile bool buttonFlag = false;

void IRAM_ATTR onTimer() {
    sampleFlag = true;
}

void IRAM_ATTR onButtonPress() {
    buttonFlag = true;
}

void loop() {
    if (sampleFlag) {
        sampleFlag = false;
        // očitanje ADC-a, filtriranje i logika vježbe
    }

    if (buttonFlag) {
        buttonFlag = false;
        // obrada tipke
    }

    // ostala neblokirajuća logika
}
```

ISR funkcije se zadržavaju kratkima kako ne bi blokirale druge događaje.

## Planirana struktura repozitorija

```text
SpiroBird/
│
├── controller/
│   ├── src/
│   │   ├── main.cpp
│   │   ├── BreathSensor.cpp
│   │   ├── BreathSensor.h
│   │   ├── ExerciseLogic.cpp
│   │   ├── ExerciseLogic.h
│   │   ├── Haptics.cpp
│   │   ├── Haptics.h
│   │   ├── EspNowSender.cpp
│   │   ├── EspNowSender.h
│   │   ├── WifiClient.cpp
│   │   ├── WifiClient.h
│   │   ├── Storage.cpp
│   │   └── Storage.h
│   └── platformio.ini
│
├── display/
│   ├── src/
│   │   ├── main.cpp
│   │   ├── EspNowReceiver.cpp
│   │   ├── EspNowReceiver.h
│   │   ├── GameRenderer.cpp
│   │   ├── GameRenderer.h
│   │   ├── UiScreens.cpp
│   │   └── UiScreens.h
│   └── platformio.ini
│
├── server/
│   ├── server.js
│   ├── package.json
│   └── results.json
│
├── docs/
│   ├── wiring.md
│   ├── protocol.md
│   ├── case-design.md
│   └── testing.md
│
└── README.md
```

## Tehnologije

Projekt koristi ili planira koristiti sljedeće tehnologije:

- ESP32-S3
- ESP32-S3 Display ES3C28P
- Arduino/C++
- PlatformIO ili Arduino IDE
- ESP-NOW komunikacija
- Wi-Fi komunikacija
- HTTP/REST API
- Node.js
- Express.js
- Render.com ili lokalni server
- NVS/Preferences memorija
- PWM/LEDC za pasivni buzzer
- 3D printano kućište
- GitHub za verzioniranje koda i dokumentaciju

## Instalacija i pokretanje

Projekt se razvija na fizičkom hardwareu. Wokwi simulacija može se koristiti samo kao pomoćni alat za provjeru pojedinih dijelova logike, ali glavni cilj je rad na stvarnim komponentama.

### 1. Priprema hardwarea

1. Spojiti potenciometar na controller ESP32-S3.
2. Spojiti pasivni buzzer na odabrani GPIO pin.
3. Spojiti vibro motor preko NPN tranzistora i flyback diode.
4. Povezati zajednički GND između ESP32-a i zasebnog napajanja motora.
5. Pripremiti display ESP32-S3 ES3C28P.
6. Postaviti oba ESP32 uređaja u isto testno okruženje.

### 2. Flashanje controller programa

1. Otvoriti `controller/` projekt.
2. Postaviti Wi-Fi podatke i adresu servera.
3. Učitati program na ESP32-S3 controller.
4. Provjeriti serijski ispis i očitanje potenciometra.

### 3. Flashanje display programa

1. Otvoriti `display/` projekt.
2. Učitati program na ESP32-S3 display.
3. Provjeriti primanje ESP-NOW paketa.
4. Provjeriti prikaz igre i trenutnih vrijednosti.

### 4. Pokretanje servera

Lokalno pokretanje Node.js servera:

```bash
cd server
npm install
node server.js
```

Nakon toga ESP32 šalje rezultate na definirani API endpoint.

### 5. Testiranje vježbe

1. Pokrenuti controller i display uređaj.
2. Pričekati kalibraciju potenciometra.
3. Pomicanjem potenciometra simulirati udah/izdah.
4. Održavati protok u ciljnoj zoni 900–1200 ml/s.
5. Pratiti let ptice, stability indikator i rezultat.
6. Provjeriti slanje rezultata na server.

## Status implementacije

| Funkcionalnost | Status |
|---|---|
| Očitavanje potenciometra | planirano |
| EMA filtriranje signala | planirano |
| Pretvorba ADC vrijednosti u ml/s | planirano |
| Detekcija zona 600/900/1200 ml/s | planirano |
| Stability indikator | planirano |
| Uvjet stabilnosti 5 sekundi | planirano |
| Fail ako protok prijeđe 1200 ml/s | planirano |
| Izračun volumena integracijom | planirano |
| ESP-NOW komunikacija između dva ESP32-a | planirano |
| Prikaz SpiroBird igre | planirano |
| Pasivni buzzer feedback | planirano |
| Vibro motor feedback | planirano |
| NVS spremanje najboljeg rezultata | planirano |
| Slanje rezultata na server | planirano |
| Render.com / Node.js backend | planirano |
| 3D printano kućište | planirano |
| Sleep mode nakon neaktivnosti | planirano |

Tijekom razvoja statusi će se mijenjati u `u izradi`, `implementirano`, `testirano` ili `odbačeno`, ovisno o stvarnom stanju projekta.

## Testiranje

Plan testiranja:

- test očitanja potenciometra
- test filtriranja signala
- test odziva bez `delay()`
- test pragova 600/900/1200 ml/s
- test stabilnog protoka 5 sekundi
- test poništavanja pokušaja iznad 1200 ml/s
- test volumena
- test buzzer signala
- test vibro motora
- test ESP-NOW komunikacije
- test prikaza igre na displayu
- test slanja rezultata serveru
- test spremanja high score vrijednosti
- test sleep/wake logike
- test rada u 3D printanom kućištu

## Mogući problemi i rizici

| Problem | Moguće rješenje |
|---|---|
| ESP-NOW i Wi-Fi server komunikacija rade na istom Wi-Fi modulu | Odvojiti slanje rezultata samo nakon završetka pokušaja |
| Motor stvara smetnje ili ruši ESP32 | Koristiti zasebno napajanje, zajednički GND, tranzistor i flyback diodu |
| Buzzer ne daje ton | Koristiti PWM/LEDC jer je buzzer pasivan |
| Potenciometar trza | Koristiti EMA filter i deadzone oko offseta |
| Display kasni | Slati male ESP-NOW pakete, a ne složene podatke |
| 5 V motor vibrira prejako | Koristiti kraće impulse ili PWM ograničenje |
| Pinovi na ESP32-S3 imaju posebne funkcije | Izbjegavati boot/USB pinove i po potrebi promijeniti pinout |

## Članovi tima

- **Hrvoje Renato Šokčić** – razvoj programskog rješenja
- **Sven Gavranović** – dokumentacija
- **Ivan Benčić** – simulacija
- **Domagoj Lepen** – testiranje

## Kontribucije

Rad na projektu dijeli se prema funkcionalnim cjelinama:

- razvoj logike očitanja i filtriranja signala
- razvoj logike uspješne/neuspješne vježbe
- razvoj SpiroBird igre i prikaza
- razvoj komunikacije između dva ESP32 uređaja
- razvoj Wi-Fi/IoT povezivosti
- razvoj zvučnog i taktilnog feedbacka
- razvoj 3D printanog kućišta
- izrada dokumentacije
- testiranje fizičkog sklopa

Članovi tima komuniciraju dogovorenim komunikacijskim kanalima, a promjene se prate kroz GitHub commitove.

## Kodeks ponašanja

Članovi tima obvezuju se na odgovorno, korektno i profesionalno ponašanje tijekom rada na projektu.

Očekuje se:

- poštivanje dogovorenih rokova
- jasna komunikacija među članovima tima
- konstruktivno rješavanje problema
- poštivanje tuđeg rada i doprinosa
- dokumentiranje vlastitih promjena
- transparentno bilježenje problema nastalih tijekom rada s fizičkim hardwareom

## Licenca

Projekt je izrađen u obrazovne svrhe u sklopu kolegija **Razvoj ugradbenih sustava** na Tehničkom veleučilištu u Zagrebu.

Materijali, biblioteke, razvojni alati i vanjske komponente podliježu vlastitim licencama.
