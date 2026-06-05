// ============================================================================
// SpiroBird backend — Express + file-based storage + static dashboard
//
// Endpoints:
//   GET    /health           liveness probe (used by the Controller too)
//   POST   /api/results      store one attempt result (sent by the Controller)
//   GET    /api/results      all results, newest first
//   GET    /api/highscore    aggregate stats / best values
//   DELETE /api/results      wipe all results (testing only)
//
// Storage: data/results.json. NOTE: on Render's free tier the filesystem is
// EPHEMERAL — data disappears on redeploy/restart. Fine for a demo; a real
// deployment would use a persistent database (see server/README.md).
// ============================================================================
"use strict";

const express = require("express");
const cors = require("cors");
const fs = require("fs");
const path = require("path");
const crypto = require("crypto");

const PORT = process.env.PORT || 3000;
const DATA_DIR = path.join(__dirname, "data");
const DATA_FILE = path.join(DATA_DIR, "results.json");

const app = express();
app.use(cors());
app.use(express.json({ limit: "32kb" }));
app.use(express.static(path.join(__dirname, "public")));

// ----------------------------------------------------------------------------
// Storage helpers
// ----------------------------------------------------------------------------

function loadResults() {
  try {
    return JSON.parse(fs.readFileSync(DATA_FILE, "utf8"));
  } catch {
    return []; // missing or corrupt file -> start clean
  }
}

function saveResults(results) {
  fs.mkdirSync(DATA_DIR, { recursive: true });
  // Write to a temp file first so a crash can't corrupt results.json.
  const tmp = DATA_FILE + ".tmp";
  fs.writeFileSync(tmp, JSON.stringify(results, null, 2));
  fs.renameSync(tmp, DATA_FILE);
}

// ----------------------------------------------------------------------------
// Routes
// ----------------------------------------------------------------------------

app.get("/health", (_req, res) => {
  res.json({ status: "ok", uptimeSec: Math.round(process.uptime()) });
});

app.post("/api/results", (req, res) => {
  const b = req.body || {};

  // Minimal validation — reject garbage, accept anything plausible.
  if (typeof b.success !== "boolean") {
    return res.status(400).json({ error: "'success' (boolean) is required" });
  }
  const num = (v) => (Number.isFinite(Number(v)) ? Number(v) : 0);

  const entry = {
    id: crypto.randomUUID(),
    deviceId: typeof b.deviceId === "string" ? b.deviceId.slice(0, 64) : "unknown",
    success: b.success,
    volumeMl: num(b.volumeMl),
    maxFlowMlS: num(b.maxFlowMlS),
    avgFlowMlS: num(b.avgFlowMlS),
    stableTimeMs: num(b.stableTimeMs),
    failReason: b.failReason == null ? null : String(b.failReason).slice(0, 32),
    timestampMs: num(b.timestampMs), // controller millis() at attempt end
    createdAt: new Date().toISOString(),
  };

  const results = loadResults();
  results.push(entry);
  saveResults(results);

  console.log(
    `[results] ${entry.deviceId} ${entry.success ? "SUCCESS" : "FAIL"} ` +
    `vol=${entry.volumeMl}ml max=${entry.maxFlowMlS} stable=${entry.stableTimeMs}ms` +
    (entry.failReason ? ` reason=${entry.failReason}` : "")
  );
  res.status(201).json(entry);
});

app.get("/api/results", (_req, res) => {
  const results = loadResults();
  results.sort((a, b) => (a.createdAt < b.createdAt ? 1 : -1)); // newest first
  res.json(results);
});

app.get("/api/highscore", (_req, res) => {
  const results = loadResults();
  const successes = results.filter((r) => r.success);

  const maxBy = (arr, key) =>
    arr.length ? arr.reduce((m, r) => (r[key] > m[key] ? r : m)) : null;

  res.json({
    attempts: results.length,
    successes: successes.length,
    bestVolume: maxBy(successes, "volumeMl"),
    bestStableTime: maxBy(results, "stableTimeMs"),
    bestMaxFlow: maxBy(results, "maxFlowMlS"),
    lastResult: results.length ? results[results.length - 1] : null,
  });
});

// Testing convenience — wipe everything.
app.delete("/api/results", (_req, res) => {
  saveResults([]);
  console.log("[results] cleared");
  res.json({ cleared: true });
});

app.listen(PORT, () => {
  console.log(`SpiroBird server listening on port ${PORT}`);
  console.log(`Dashboard: http://localhost:${PORT}/`);
});
