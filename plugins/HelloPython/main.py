#!/usr/bin/env python3
"""MPMPES process plugin — newline JSON on stdin/stdout."""
import json
import sys


def send(obj):
    sys.stdout.write(json.dumps(obj, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def log(msg, level="info"):
    send({"op": "log", "level": level, "msg": msg})


def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except json.JSONDecodeError:
            continue
        op = msg.get("op")
        if op == "init":
            log("HelloPython: init OK")
            send({"op": "ok"})
        elif op == "shutdown":
            log("HelloPython: shutdown")
            break
        elif op == "event":
            name = msg.get("name")
            data = msg.get("data") or {}
            if name == "server_start":
                log(f"HelloPython: server_start motd={data.get('motd')} port={data.get('port')}")
            elif name == "session_open":
                log(f"HelloPython: session_open {data.get('address')}:{data.get('port')}")
            elif name == "session_close":
                log(f"HelloPython: session_close {data.get('reason')}")
            elif name == "player_login":
                log(f"HelloPython: player_login {data.get('username')} world={data.get('world')}")
            elif name == "player_join":
                log(f"HelloPython: player_join {data.get('username')} @ {data.get('world')}")
            elif name == "player_quit":
                log(f"HelloPython: player_quit {data.get('username')}")
            elif name == "chat":
                log(f"HelloPython: chat <{data.get('username')}> {data.get('message')}")
            elif name == "command":
                log(f"HelloPython: command /{data.get('command')} {data.get('args')}")
            elif name == "world_load":
                log(f"HelloPython: world_load {data.get('name')} gen={data.get('generator')}")
            elif name == "block":
                pass  # high-freq dig/place — never log (spams console)
            else:
                # skip high-freq noise
                if name not in ("move",):
                    log(f"HelloPython: event {name}", level="info")


if __name__ == "__main__":
    main()
