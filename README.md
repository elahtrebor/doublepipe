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

#
