// SpiroBird dashboard — polls the API every 3 s and renders cards + table.
"use strict";

const REFRESH_MS = 3000;
const $ = (id) => document.getElementById(id);

const fmtL = (ml) => (ml / 1000).toFixed(2) + " L";
const fmtS = (ms) => (ms / 1000).toFixed(1) + " s";
const fmtFlow = (f) => Math.round(f) + " ml/s";
const fmtTime = (iso) => new Date(iso).toLocaleString();

async function refresh() {
  try {
    const [hsRes, listRes] = await Promise.all([
      fetch("/api/highscore"),
      fetch("/api/results"),
    ]);
    if (!hsRes.ok || !listRes.ok) throw new Error("API error");
    const hs = await hsRes.json();
    const results = await listRes.json();

    $("status").textContent = "server online";
    $("status").className = "status ok";

    renderCards(hs);
    renderTable(results);
  } catch {
    $("status").textContent = "server unreachable";
    $("status").className = "status err";
  }
}

function renderCards(hs) {
  $("attempts").textContent = hs.attempts;
  $("successRate").textContent =
    hs.attempts > 0
      ? `${hs.successes} successful (${Math.round((hs.successes / hs.attempts) * 100)} %)`
      : "";

  $("bestVolume").textContent = hs.bestVolume ? fmtL(hs.bestVolume.volumeMl) : "–";
  $("bestVolumeSub").textContent = hs.bestVolume
    ? `${hs.bestVolume.deviceId} · ${fmtTime(hs.bestVolume.createdAt)}`
    : "no successful attempt yet";

  $("bestStable").textContent = hs.bestStableTime ? fmtS(hs.bestStableTime.stableTimeMs) : "–";
  $("bestFlow").textContent = hs.bestMaxFlow ? fmtFlow(hs.bestMaxFlow.maxFlowMlS) : "–";

  if (hs.lastResult) {
    $("lastResult").textContent = hs.lastResult.success ? "SUCCESS" : "FAIL";
    $("lastResult").className = "big " + (hs.lastResult.success ? "ok" : "fail");
    $("lastResultSub").textContent =
      `${fmtL(hs.lastResult.volumeMl)} · ${fmtTime(hs.lastResult.createdAt)}`;
  } else {
    $("lastResult").textContent = "–";
    $("lastResultSub").textContent = "";
  }
}

function renderTable(results) {
  const tbody = document.querySelector("#resultsTable tbody");
  $("empty").hidden = results.length > 0;

  tbody.innerHTML = results
    .slice(0, 100) // keep the page light
    .map(
      (r) => `
      <tr>
        <td>${fmtTime(r.createdAt)}</td>
        <td>${escapeHtml(r.deviceId)}</td>
        <td class="${r.success ? "ok" : "fail"}">${r.success ? "SUCCESS" : "FAIL"}</td>
        <td>${fmtL(r.volumeMl)}</td>
        <td>${fmtFlow(r.maxFlowMlS)}</td>
        <td>${fmtFlow(r.avgFlowMlS)}</td>
        <td>${fmtS(r.stableTimeMs)}</td>
        <td>${r.failReason ? escapeHtml(r.failReason) : "—"}</td>
      </tr>`
    )
    .join("");
}

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) =>
    ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c])
  );
}

$("refreshBtn").addEventListener("click", refresh);
refresh();
setInterval(refresh, REFRESH_MS);
