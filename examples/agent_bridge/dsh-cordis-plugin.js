"use strict";

const { execFile } = require("node:child_process");
const path = require("node:path");

// Verified against the installed DSH 0.2.1-rc.2 session/event contract.
// Observe committed records only; never participate in approval/request's waterfall.
exports.name = "qt-desktop-pet-status";

function text(value) { return typeof value === "string" ? value.trim() : ""; }
function safe(value) {
  const raw = text(value);
  if (/(?:\bsk-[\w-]{12,}|\bAIza[\w-]{20,}|\bghp_\w{20,}|\bgithub_pat_\w{20,}|\bAKIA[0-9A-Z]{16}|Bearer\s+\S{12,}|(?:api[_ -]?key|access[_ -]?token|password|secret|密码|密钥)\s*[:：=]\s*\S{4,})/i.test(raw)) return "已隐藏敏感信息";
  return raw.replace(/\s+/g, " ").slice(0, 120);
}
function argsObject(value) {
  if (value && typeof value === "object" && !Array.isArray(value)) return value;
  try { return JSON.parse(value) || {}; } catch (_) { return {}; }
}
function summary(tool, value) {
  const args = argsObject(value);
  return safe(args.command || args.cmd || args.query || args.pattern ||
    path.win32.basename(text(args.file_path || args.path).replace(/\//g, "\\")) || tool);
}

exports.mapSessionEvent = function mapSessionEvent(session, event) {
  if (!session || !text(session.id) || !event || typeof event.type !== "string") return null;
  const data = event.data || {};
  const packet = {
    protocol: 1, source: "dsh", sessionId: text(session.id),
    workspace: path.win32.basename(text(session.header?.cwd).replace(/\//g, "\\")),
    state: "working", summary: "", turnId: data.turn == null ? "" : String(data.turn),
    eventId: event.seq == null ? "" : "seq:" + event.seq, ttlSec: 180
  };
  switch (event.type) {
    case "turn/start":
      packet.eventKind = "turn_start"; packet.summary = "正在处理任务"; break;
    case "tool/call":
      packet.eventKind = "tool";
      packet.activity = { id: text(data.callId), tool: safe(data.name), summary: summary(data.name, data.arguments), phase: "started" };
      break;
    case "tool/result": {
      const message = data.message || {};
      packet.eventKind = "tool";
      packet.activity = { id: text(message.source?.callId || message.toolCallId), tool: "", summary: "", phase: message.isError === true ? "failed" : "finished" };
      // Results carry no stdout, model messages, error bodies, or tool-private meta.
      break;
    }
    case "approval/asked":
      packet.eventKind = "attention"; packet.state = "needs_input"; packet.summary = "等待你确认"; break;
    case "approval/decided":
      packet.eventKind = "attention"; packet.state = "working"; packet.summary = "确认流程已结束"; break;
    case "turn/end": {
      const reason = typeof data.reason === "string" ? data.reason : data.reason?.kind;
      packet.eventKind = "turn_end";
      packet.state = reason === "error" ? "error" : reason === "blocked" ? "needs_input" :
        reason === "completed" ? "done" : "idle";
      packet.summary = reason === "completed" ? "本轮任务已完成" : reason === "error" ? "任务遇到问题" :
        reason === "blocked" ? "任务等待处理" : "本轮已结束";
      break;
    }
    default: return null;
  }
  return packet;
};

exports.apply = function apply(ctx, config = {}) {
  if (!ctx || typeof ctx.on !== "function") return;
  const executable = text(config.bridgeExePath) || text(process.env.QTDESKTOPPET_BRIDGE_EXE);
  if (!executable) return;
  const timeout = Number.isFinite(config.timeoutMs) ? Math.max(100, Math.min(5000, config.timeoutMs)) : 1500;
  const queue = [];
  const callMetadata = new Map();
  let running = false;
  let disposed = false;
  function drain() {
    if (running || disposed || !queue.length) return;
    running = true;
    const packet = queue.shift();
    const args = ["--packet-stdin"];
    if (text(config.pipeName)) args.push("--pipe", config.pipeName.trim());
    try {
      const child = execFile(executable, args, { timeout, windowsHide: true, maxBuffer: 4096 }, () => {
        running = false;
        drain();
      });
      child.stdin.on("error", () => {});
      child.stdin.end(JSON.stringify(packet));
    } catch (_) { running = false; queueMicrotask(drain); }
  }
  function send(packet) {
    if (disposed || !packet) return;
    // Bounded asynchronous delivery: never await IPC from the DSH event listener.
    if (queue.length >= 256) queue.shift();
    queue.push(packet);
    drain();
  }
  ctx.on("session/event", (session, event) => {
    try {
      const packet = exports.mapSessionEvent(session, event);
      if (!packet) return;
      if (packet.activity) {
        const key = packet.sessionId + ":" + packet.turnId + ":" + packet.activity.id;
        if (event.type === "tool/call") {
          callMetadata.set(key, { tool: packet.activity.tool, summary: packet.activity.summary });
          if (callMetadata.size > 1000) callMetadata.delete(callMetadata.keys().next().value);
        } else {
          const metadata = callMetadata.get(key);
          if (metadata) Object.assign(packet.activity, metadata);
          else packet.activity.tool = "tool";
          callMetadata.delete(key);
        }
      }
      send(packet);
    } catch (_) { /* The observer must never interrupt DSH. */ }
  }, { global: true });
  ctx.on("session/disposed", (session) => {
    if (!session || !text(session.id)) return;
    send({ protocol: 1, source: "dsh", sessionId: session.id, state: "idle", eventKind: "session_end", summary: "对话已结束" });
    for (const key of callMetadata.keys()) if (key.startsWith(session.id + ":")) callMetadata.delete(key);
  }, { global: true });
  ctx.on("dispose", () => { disposed = true; queue.length = 0; callMetadata.clear(); });
};
