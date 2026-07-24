// Minimal process plugin — build: cargo build --release
// then copy target/release/hello_rust to plugins/HelloRust/hello_rust
use std::io::{self, BufRead, Write};

fn send(obj: &str) {
    let mut out = io::stdout();
    let _ = writeln!(out, "{}", obj);
    let _ = out.flush();
}

fn logmsg(msg: &str) {
    // keep JSON simple without serde dependency
    let escaped = msg.replace('\\', "\\\\").replace('"', "\\\"");
    send(&format!(r#"{{"op":"log","level":"info","msg":"{}"}}"#, escaped));
}

fn main() {
    let stdin = io::stdin();
    for line in stdin.lock().lines() {
        let Ok(line) = line else { break };
        let line = line.trim().to_string();
        if line.is_empty() {
            continue;
        }
        if line.contains(r#""op":"init""#) || line.contains(r#""op": "init""#) {
            logmsg("HelloRust: init OK");
            send(r#"{"op":"ok"}"#);
        } else if line.contains(r#""op":"shutdown""#) || line.contains(r#""op": "shutdown""#) {
            logmsg("HelloRust: shutdown");
            break;
        } else if line.contains(r#""name":"server_start""#) {
            logmsg("HelloRust: server_start");
        } else if line.contains(r#""name":"player_login""#) {
            logmsg("HelloRust: player_login");
        } else if line.contains(r#""name":"session_open""#) {
            logmsg("HelloRust: session_open");
        }
    }
}
