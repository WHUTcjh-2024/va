(() => {
  "use strict";
  const sleep = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));
  const percentile = (values, p) => values.length ? values.slice().sort((a, b) => a - b)[Math.ceil((values.length - 1) * p)] : "";
  const formatNumber = value => Number.isFinite(value) ? value.toFixed(2) : "";
  const csvEscape = value => `"${String(value).replaceAll('"', '""')}"`;
  const downloadCsv = (records, summary) => { const header = ["run_id", "round_id", "expected_code", "submitted_code", "outcome", "t0_unix_ms", "t1_unix_ms", "latency_ms"]; const rows = records.map(record => header.map(key => csvEscape(record[key] ?? "")).join(",")); const footer = [[], ["metric", "value"], ...Object.entries(summary)].map(row => row.map(csvEscape).join(",")); const blob = new Blob([[header.join(","), ...rows, ...footer].join("\r\n")], { type: "text/csv;charset=utf-8" }); const link = Object.assign(document.createElement("a"), { href: URL.createObjectURL(blob), download: "val-invite-results.csv" }); link.click(); setTimeout(() => URL.revokeObjectURL(link.href), 0); };
  window.ValInviteBenchmark = Object.freeze({ DEFAULTS: { rounds: 100, revealMs: 800, timeoutMs: 1500, gapMs: 250, jitterMs: 180 }, sleep, percentile, formatNumber, downloadCsv, onMessage: () => () => {}, post: () => {} });
})();
