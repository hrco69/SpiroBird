# SpiroBird — Izvještaj o simulaciji i verifikaciji logike

> **Zadužen za simulaciju:** Ivan Benčić
> **Alati:** Python 3.12, `sim/spiro_model.py` (1:1 port firmware logike),
> `test_logic.py`, `simulate_breath.py`, `flappy_preview.html`

## 1. Pristup

Logika vježbe (mapiranje ADC→protok, EMA filter, detekcija stabilnosti, integracija
volumena, state machine) portana je 1:1 u Python s identičnim konstantama kao u
`controller/include/config.h`. Time je cijela logika verificirana **prije flashanja
na hardver** — pogreške u algoritmu hvatale su se u milisekundama umjesto kroz
flash-test cikluse.

## 2. Pokretanje

```sh
cd sim
python test_logic.py        # 23 unit testa
python simulate_breath.py   # 5 scenarija + generira sample_packets.json
python -m http.server 8000  # vizualni pregled: flappy_preview.html
```

## 3. Unit testovi — 23/23 prolaze

| Kategorija | Testovi | Što se provjerava |
|---|---|---|
| EMA filter | 3 | formula prvog koraka (α·x), konvergencija, prigušenje šiljaka |
| Mapiranje protoka | 6 | mirovanje→0, rub deadzone→0, max otklon→1400, monotonost, oba smjera, roundtrip |
| Stabilnost | 4 | konstanta u zoni = stabilno; nepopunjen prozor ≠ stabilno; velika varijanca ≠ stabilno; izvan zone ≠ stabilno |
| Volumen | 2 | integracija konstantnog protoka, nulti protok = 0 |
| Uspjeh/neuspjeh | 6 | uspjeh točno nakon ≥5000 ms; FAIL_OVER_1200 (filtrirano i sirovi šiljak); FAIL_TIMEOUT; slijed tranzicija |
| Protokol | 2 | binarni layout = točno 48 B (cross-check C++ `static_assert`); checksum detektira flip jednog bita |

## 4. Scenariji (`simulate_breath.py`) — 5/5 prolaze

| # | Scenarij | Očekivano | Rezultat |
|---|---|---|---|
| 1 | Stabilan protok ~1000 ml/s (+ ADC šum) | SUCCESS nakon 5 s stabilnosti | ✅ SUCCESS @ 6.8 s, volumen 5506 ml, stable 5000 ms |
| 2 | Rampa preko 1200 ml/s | FAIL_OVER_1200 | ✅ FAIL @ 3.9 s |
| 3 | Nemiran protok unutar dozvoljenih granica (max 1179) | odbija ga isključivo stability gate | ✅ FAIL_TIMEOUT — nikad stabilan, nikad preko limita |
| 4 | Slab protok 450 ml/s | nikad ne uđe u zonu | ✅ FAIL_TIMEOUT |
| 5 | Sićušan protok 80 ml/s (ispod praga starta) | pokušaj ne kreće | ✅ ostaje u STATE_READY |

Primjer ispisa tranzicija (scenarij 1) — identičan format kao firmware Serial:

```txt
[    10 ms] STATE_IDLE -> STATE_CALIBRATING
[  2010 ms] STATE_CALIBRATING -> STATE_READY
[  2210 ms] STATE_READY -> STATE_ACTIVE
[  7770 ms] STATE_ACTIVE -> STATE_SUCCESS
[  8970 ms] STATE_SUCCESS -> STATE_RESULT
result: SUCCESS  volume=5506 ml  stable=5000 ms
```

## 5. Podudaranje simulacije i stvarnog hardvera

Ključna vrijednost pristupa: ponašanja predviđena simulacijom potvrđena su kasnije na
fizičkom uređaju bez izmjena logike:

- **Scenarij 2 ≈ stvarnost:** svi neuspjesi u hardverskom testiranju (10/10) bili su
  `FAIL_OVER_1200` — isti mehanizam koji simulacija demonstrira.
- **Scenarij 3 dokazuje stability gate:** protok unutar granica, ali p2p varijacija
  > 180 ml/s → nema uspjeha. Na hardveru se to očituje kao "moraš MIRNO držati zonu".
- **Sirovi šiljak (unit test):** zaštita `raw > 1250` reagirala je u simulaciji
  identično kao na šumovitom potenciometru.
- Redizajn kalibracije s fiksnim centrom (hardverski nalaz) odmah je zrcaljen u
  `spiro_model.py` — svi testovi i dalje prolaze, čime je potvrđeno da izmjena ne
  kvari logiku vježbe.

## 6. Vizualni pregled

`flappy_preview.html` reproducira `sample_packets.json` (160 paketa @ 40 Hz iz
scenarija 1) s identičnim konstantama fizike ptice kao Display firmware — služi za
pregled izgleda igre bez hardvera.

## 7. Zaključak

Logika vježbe je formalno verificirana (23 unit testa) i scenarijski validirana
(5 scenarija) prije ijednog flashanja. Model i firmware dijele konstante, pa svaka
buduća promjena pragova u `config.h` zahtijeva samo ažuriranje vrijednosti na vrhu
`spiro_model.py` i ponovno pokretanje testova.
