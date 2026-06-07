# SpiroBird — Dizajn 3D printanog kućišta

> Status: smjernice za izradu (kućište je planirani dodatak, ne uvjet za demo).

## Kućište Controllera (Master)

### Sadržaj
ESP32-S3 dev ploča, potenciometar B10K (osovina kroz kućište), vibracijski motor,
NPN driver pločica (tranzistor + otpornici + dioda), wake gumb (rocker), ulaz 5 V napajanja.

### Zahtjevi
| Element | Zahtjev |
|---|---|
| Potenciometar | montaža na prednju plohu navojem ili press-fit Ø7 mm; osovina van; VELIKI knob (precizno doziranje "daha" je gameplay!) |
| Wake gumb | prednja ploha, dostupan jednim prstom (budi iz deep sleepa) |
| RGB LED | svjetlovod ili prozirni prozorčić iznad onboard LED-ice (statusna indikacija!) |
| Vibro motor | čvrsto uz kućište radi prijenosa vibracije na ruku/stol; ne lijepiti uz USB konektor |
| USB portovi | izrez za oba USB-C (flash + napajanje) |
| BOOT/RST | rupice Ø2 mm za pristup čačkalicom (servis) |
| Ventilacija | prorezi iznad ESP32 modula |
| Dno | 4× M3 stupić za ploču ili utori; gumene nožice |

### Preporučene dimenzije
~100 × 70 × 35 mm (ploča 25×50 mm + driver pločica + motor + kabliranje).

## Kućište Displaya (Slave)

- Okvir oko 2.8" panela: vidljivo polje ~58 × 43 mm, **otvor preko cijelog touch
  područja** (touch budi ekran iz sleepa!)
- Nagib 60–75° od horizontale (stolni stalak) — igrač gleda ekran dok "puše"
- Izrez za USB-C (napajanje), rupice za BOOT/RST
- Bez pokrivanja antene modula (ESP-NOW prijem) — ne stavljati metalne dijelove uz
  gornji rub ploče

## Materijal i print

- **PLA** dovoljan (nema grijanja); PETG ako će stajati na suncu
- Sloj 0.2 mm, 3 perimetra, infill 20 %
- Tolerancije: rupe +0.2 mm na nazivnu mjeru; press-fit utori −0.1 mm
- Dvodijelno (donja školjka + poklopac), M3 vijci u ugrijane mesing inserte ili
  samourezni u stupiće

## Napomena za demo

Za obranu je sustav potpuno funkcionalan i bez kućišta (breadboard + stalak);
kućište je estetski/ergonomski dodatak i može se printati naknadno bez ikakvih
izmjena u softveru.
