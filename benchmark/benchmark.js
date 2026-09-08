(() => {
  "use strict";

  const CHANNEL_NAME = "val-invite-benchmark-v1";
  const DEFAULTS = Object.freeze({ rounds: 100, revealMs: 800, timeoutMs: 1500, gapMs: 250 });
  const channel = new BroadcastChannel(CHANNEL_NAME);
  const listeners = new Set();

  channel.addEventListener("message", (event) => {
    if (!event.data || typeof event.data !== "object") return;
    for (const listener of listeners) listener(event.data);
  });

  const onMessage = (listener) => {
    listeners.add(listener);
    return () => listeners.delete(listener);
  };

  const post = (message) => channel.postMessage({ ...message, sentAt: Date.now() });
  const sleep = (milliseconds) => new Promise((resolve) => window.setTimeout(resolve, milliseconds));

  const percentile = (values, p) => {
    if (values.length === 0) return "";
    const index = Math.ceil((values.length - 1) * p);
    return values.slice().sort((a, b) => a - b)[index];
  };

  const formatNumber = (value) => Number.isFinite(value) ? value.toFixed(2) : "";

  const csvEscape = (value) => `"${String(value).replaceAll('"', '""')}"`;

  const downloadCsv = (records, summary, filename = "results.csv") => {
    const header = ["run_id", "round_id", "expected_code", "submitted_code", "outcome", "t0_unix_ms", "t1_unix_ms", "latency_ms"];
    const rows = records.map((record) => header.map((key) => csvEscape(record[key] ?? "")).join(","));
    const summaryRows = [
      [],
      ["metric", "value"],
      ...Object.entries(summary).map(([key, value]) => [key, value]),
    ].map((row) => row.map(csvEscape).join(","));
    const blob = new Blob([[header.join(","), ...rows, ...summaryRows].join("\r\n")], { type: "text/csv;charset=utf-8" });
    const link = document.createElement("a");
    link.href = URL.createObjectURL(blob);
    link.download = filename;
    link.click();
    window.setTimeout(() => URL.revokeObjectURL(link.href), 0);
  };

  window.ValInviteBenchmark = Object.freeze({
    CHANNEL_NAME,
    DEFAULTS,
    onMessage,
    post,
    sleep,
    percentile,
    formatNumber,
    downloadCsv,
  });
})();
