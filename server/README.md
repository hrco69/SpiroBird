# SpiroBird backend

Node.js/Express server koji prima rezultate vježbi s Controller ESP32-S3 uređaja i
prikazuje live dashboard.

## Pokretanje lokalno

```sh
cd server
npm install
npm start          # http://localhost:3000
```

Port se mijenja varijablom okoline: `PORT=8080 npm start`
(Windows PowerShell: `$env:PORT=8080; npm start`).

## API

| Metoda | Ruta | Opis |
|---|---|---|
| GET | `/health` | liveness probe — `{"status":"ok"}` |
| POST | `/api/results` | sprema jedan rezultat (šalje Controller) |
| GET | `/api/results` | svi rezultati, najnoviji prvi |
| GET | `/api/highscore` | agregati: najbolji volumen/stabilnost/protok, zadnji rezultat |
| DELETE | `/api/results` | briše sve (samo za testiranje) |

### POST /api/results — tijelo zahtjeva

```json
{
  "deviceId": "spirobird-01",
  "success": true,
  "volumeMl": 5120,
  "maxFlowMlS": 1080,
  "avgFlowMlS": 965,
  "stableTimeMs": 5000,
  "failReason": null,
  "timestampMs": 123456
}
```

### Test curl naredbama (bring-up korak 4)

```sh
curl http://localhost:3000/health

curl -X POST http://localhost:3000/api/results \
  -H "Content-Type: application/json" \
  -d '{"deviceId":"test","success":true,"volumeMl":5120,"maxFlowMlS":1080,"avgFlowMlS":965,"stableTimeMs":5000,"failReason":null,"timestampMs":1}'

curl http://localhost:3000/api/results
curl http://localhost:3000/api/highscore
curl -X DELETE http://localhost:3000/api/results
```

PowerShell ekvivalent:

```powershell
Invoke-RestMethod http://localhost:3000/health
Invoke-RestMethod -Method Post -Uri http://localhost:3000/api/results -ContentType "application/json" -Body '{"deviceId":"test","success":true,"volumeMl":5120,"maxFlowMlS":1080,"avgFlowMlS":965,"stableTimeMs":5000,"failReason":null,"timestampMs":1}'
```

## Deploy na Render.com

1. Pushati repozitorij na GitHub.
2. Render → **New → Web Service** → povezati GitHub repo.
3. Postavke:
   - **Root Directory:** `server`
   - **Build Command:** `npm install`
   - **Start Command:** `npm start`
   - **Instance Type:** Free
4. Render automatski postavlja `PORT` varijablu — server je čita iz okoline.
5. Nakon deploya: `https://<ime-servisa>.onrender.com/health` mora vratiti `{"status":"ok"}`.
6. URL upisati u `controller/include/secrets.h`:
   ```cpp
   #define SERVER_BASE_URL "https://<ime-servisa>.onrender.com"
   ```

### ⚠️ Ograničenja besplatnog Rendera (poznato i prihvaćeno za demo)

- **Efemeran disk**: `data/results.json` se BRIŠE pri svakom redeployu/restartu.
  Za pravu produkciju koristila bi se trajna baza (PostgreSQL/SQLite na disku).
- **Spin-down**: besplatni servis zaspi nakon ~15 min neaktivnosti; prvi sljedeći
  zahtjev traje 30–60 s. Controller ima HTTP timeout 1500 ms → prvi POST nakon
  spavanja može propasti, a sljedeći prolazi (retry logika to pokriva).
  Za demo: otvoriti dashboard par minuta prije obrane da se servis probudi.
