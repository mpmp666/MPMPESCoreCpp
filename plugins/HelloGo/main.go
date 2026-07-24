package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"os"
)

func send(v any) {
	b, _ := json.Marshal(v)
	fmt.Println(string(b))
}

func logmsg(msg, level string) {
	send(map[string]any{"op": "log", "level": level, "msg": msg})
}

func main() {
	sc := bufio.NewScanner(os.Stdin)
	for sc.Scan() {
		line := sc.Text()
		if line == "" {
			continue
		}
		var msg map[string]any
		if err := json.Unmarshal([]byte(line), &msg); err != nil {
			continue
		}
		op, _ := msg["op"].(string)
		switch op {
		case "init":
			logmsg("HelloGo: init OK", "info")
			send(map[string]any{"op": "ok"})
		case "shutdown":
			logmsg("HelloGo: shutdown", "info")
			return
		case "event":
			name, _ := msg["name"].(string)
			data, _ := msg["data"].(map[string]any)
			switch name {
			case "server_start":
				logmsg(fmt.Sprintf("HelloGo: server_start motd=%v port=%v", data["motd"], data["port"]), "info")
			case "player_login":
				logmsg(fmt.Sprintf("HelloGo: player_login %v protocol=%v", data["username"], data["protocol"]), "info")
			case "session_open":
				logmsg(fmt.Sprintf("HelloGo: session_open %v:%v", data["address"], data["port"]), "info")
			case "block", "move":
				// high-freq — never log (spams console on dig/place)
			default:
				logmsg("HelloGo: event "+name, "info")
			}
		}
	}
}
