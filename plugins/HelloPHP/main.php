#!/usr/bin/env php
<?php
// MPMPES process plugin — newline JSON on stdin/stdout

function send(array $obj): void {
    fwrite(STDOUT, json_encode($obj, JSON_UNESCAPED_UNICODE) . "\n");
    fflush(STDOUT);
}

function logmsg(string $msg, string $level = "info"): void {
    send(["op" => "log", "level" => $level, "msg" => $msg]);
}

$stdin = fopen("php://stdin", "r");
while (($line = fgets($stdin)) !== false) {
    $line = trim($line);
    if ($line === "") continue;
    $msg = json_decode($line, true);
    if (!is_array($msg)) continue;
    $op = $msg["op"] ?? "";
    if ($op === "init") {
        logmsg("HelloPHP: init OK");
        send(["op" => "ok"]);
    } elseif ($op === "shutdown") {
        logmsg("HelloPHP: shutdown");
        break;
    } elseif ($op === "event") {
        $name = $msg["name"] ?? "";
        $data = $msg["data"] ?? [];
        if ($name === "server_start") {
            logmsg("HelloPHP: server_start motd=" . ($data["motd"] ?? "") . " port=" . ($data["port"] ?? ""));
        } elseif ($name === "player_login") {
            logmsg("HelloPHP: player_login " . ($data["username"] ?? "") . " protocol=" . ($data["protocol"] ?? ""));
        } elseif ($name === "session_open") {
            logmsg("HelloPHP: session_open " . ($data["address"] ?? "") . ":" . ($data["port"] ?? ""));
        } else {
            logmsg("HelloPHP: event $name");
        }
    }
}
