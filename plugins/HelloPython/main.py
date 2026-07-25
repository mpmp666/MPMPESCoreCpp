#!/usr/bin/env python3
"""MPMPES process plugin — newline JSON on stdin/stdout. API v3."""
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
            log(f"HelloPython: init OK (api={msg.get('api')})")
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
            elif name == "sign_change":
                log(
                    "HelloPython: sign_change "
                    f"{data.get('username')} @ {data.get('x')},{data.get('y')},{data.get('z')} "
                    f"|{data.get('text1')}|{data.get('text2')}|{data.get('text3')}|{data.get('text4')}|"
                )
                # Example: cancel empty first line rewrites are no-ops.
                # To cancel: send({"op":"cancel_sign"})
                # To rewrite: send({"op":"rewrite_sign","text1":"...","text2":"...","text3":"...","text4":"..."})
            elif name == "block":
                pass  # high-freq dig/place — never log (spams console)
            else:
                if name not in ("move",):
                    log(f"HelloPython: event {name}", level="info")


if __name__ == "__main__":
    main()
