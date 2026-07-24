#!/usr/bin/env node
"use strict";

const readline = require("readline");

function send(obj) {
  process.stdout.write(JSON.stringify(obj) + "\n");
}

function log(msg, level = "info") {
  send({ op: "log", level, msg });
}

const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });

rl.on("line", (line) => {
  line = line.trim();
  if (!line) return;
  let msg;
  try {
    msg = JSON.parse(line);
  } catch {
    return;
  }
  const op = msg.op;
  if (op === "init") {
    log("HelloNode: init OK");
    send({ op: "ok" });
  } else if (op === "shutdown") {
    log("HelloNode: shutdown");
    process.exit(0);
  } else if (op === "event") {
    const name = msg.name;
    const data = msg.data || {};
    if (name === "server_start") {
      log(`HelloNode: server_start motd=${data.motd} port=${data.port}`);
    } else if (name === "player_login") {
      log(`HelloNode: player_login ${data.username} protocol=${data.protocol}`);
    } else if (name === "session_open") {
      log(`HelloNode: session_open ${data.address}:${data.port}`);
    } else {
      log(`HelloNode: event ${name}`);
    }
  }
});
