# Double Pipe (dp)

> Run remote commands and pipe the output through your **local UNIX tools** — even when the remote system doesn’t have them.

---

## 🚀 Overview

**Double Pipe (`dp`)** is a lightweight PTY-based tool that lets you augment remote shells (SSH, routers, servers, embedded systems) with **local processing power**.

It enables workflows like:

```bash
show run || grep interface
show log || grep ERROR || wc -l
```

Even if the remote system has no `grep`, `awk`, or `sed`, you can still use them locally — seamlessly.

---

## ✨ Features

### 🔌 Local Pipes for Remote Commands

* Use `||` to pipe remote output into local commands
* Supports **multi-stage pipelines**:

  ```bash
  show run || grep interface || sort || uniq -c
  ```

---

### 🔐 Safe Interactive Sessions

* No password echo (raw no-echo terminal mode)
* Control keys (`Ctrl-C`, `Ctrl-Z`, etc.) behave correctly
* Minimal interference with normal terminal behavior

---

### ⚡ Streaming & Smart Pipeline Handling

* Streams remote output directly into local tools
* Automatically closes pipelines (fixes `wc -l`, `sort`, etc.)
* No extra Enter required — prompt refresh handled automatically

---

### 🎛 Escape Mode (Optional)

```bash
--escape
```

Press `Ctrl-]` (default) to enter local command mode:

```text
Ctrl-]
local> !ls
local> resume
```

Commands:

* `resume` / `r` → return to session
* `quit` / `q` → exit
* `!cmd` → run local shell command
* `help` → show help

---

### 📜 Logging

```bash
--log session.txt
```

* Logs remote output and internal events
* Does NOT log raw keyboard input (safe for passwords)

---

### 🐞 Debug Mode

```bash
--debug
```

* Shows internal state:

  * pipeline start/stop
  * escape mode transitions

---

### ⏱ Configurable Idle Timeout

```bash
--idle-ms 700
```

* Controls when local pipelines close
* Default: ~350ms
* Useful for slower devices

---

## 🛠 Installation

```bash
git clone https://github.com/elahtrebor/dp.git
cd dp
cc -Wall -Wextra -O2 dp_featured.c -o dp
```

---

## ▶️ Usage

```bash
./dp ssh user@router
```

Then inside the session:

```bash
show run || grep interface
show log || grep ERROR || wc -l
show interfaces || awk '/up/ {print $1,$2}'
```

---

## 💡 Tips

### Disable paging on network devices

For best results:

```bash
terminal length 0
```

---

### Works great for:

* Routers (Cisco, Juniper, etc.)
* Embedded systems
* Restricted shells
* Legacy systems without modern tools

---

## 🌍 Portability

Tested on:

* Linux (Red Hat, Ubuntu, etc.)
* macOS
* WSL (Windows Subsystem for Linux)

Uses standard POSIX APIs:

* `forkpty()`
* `termios`
* `poll()`
* `ioctl()`

---

## 🧠 How It Works

Double Pipe creates a **PTY bridge** between your terminal and the remote system:

```
keyboard → dp → PTY → remote system
remote output → dp → local pipeline → screen
```

It allows local tools to process remote output in real-time.

---

## 🔥 Example Workflows

### Count interfaces on a router

```bash
show run || grep interface || wc -l
```

### Find active interfaces

```bash
show interfaces || awk '/up/ {print $1,$2}'
```

### Analyze logs

```bash
show log || grep ERROR || sort | uniq -c
```

---

## ⚠️ Notes

* Prompt detection is intentionally avoided for robustness
* Idle-based pipeline closing is used instead
* Native Windows (non-WSL) is not supported

---

## 📌 Roadmap

* Prompt-aware pipeline closing
* Enhanced pipeline chaining
* Built-in helpers (json, table, etc.)
* AI/LLM integration hooks

---

## 🧩 Summary

Double Pipe turns any remote CLI into a **locally-augmented shell**:

> **SSH + local UNIX pipeline engine + interactive PTY bridge**

---

## 🤝 Contributing

PRs and ideas welcome — especially around:

* portability
* performance
* new pipeline features

---

## 📄 License

MIT License (or your preferred license)

---

## 👤 Author

Robert Hale (rhale)

---

## ⭐ If you find this useful

Give it a star on GitHub!
