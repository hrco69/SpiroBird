# SpiroBird — simulacija i testovi

Verifikacija logike vježbe **prije** flashanja na hardver. `spiro_model.py` je
1:1 Python port firmware logike (`BreathSensor.cpp` + `ExerciseLogic.cpp`) s
identičnim konstantama iz `controller/include/config.h`.

> ⚠️ Ako mijenjaš konstante u `controller/include/config.h`, ažuriraj ih i u
> `sim/spiro_model.py` (vrijednosti su navedene na vrhu datoteke).

## Pokretanje

```sh
cd sim

# 1) Unit testovi (EMA, mapiranje protoka, stabilnost, volumen, uspjeh/neuspjeh,
#    layout i checksum SpiroPacketa) — 23 testa
python test_logic.py

# 2) Scenariji s ispisom tranzicija stanja (kao Serial na firmwareu)
#    + generira sample_packets.json
python simulate_breath.py

# 3) Vizualni pregled igre u browseru (opcionalno)
python -m http.server 8000
# otvoriti: http://localhost:8000/flappy_preview.html
```

## Scenariji u `simulate_breath.py`

| # | Scenarij | Očekivani ishod |
|---|---|---|
| 1 | Stabilan protok ~1000 ml/s | SUCCESS nakon 5 s stabilnosti |
| 2 | Rampa preko 1200 ml/s | FAIL_OVER_1200 |
| 3 | Nemiran protok unutar granica (900–1180) | FAIL_TIMEOUT — odbija ga isključivo stability gate (p2p > 180) |
| 4 | Slab protok 450 ml/s | FAIL_TIMEOUT — nikad ne uđe u zonu |
| 5 | Sićušan protok 80 ml/s | ostaje u STATE_READY (ispod praga starta) |

Exit code 0 = svi scenariji prošli (pogodno za CI).
