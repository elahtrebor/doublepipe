# doublepipe
A terminal utility that allows to pipe remote session output to local resources.
<br>
This code based on: http://rachid.koucha.free.fr/tech_corner/pty_pdip.html - examples for psuedo terminals.
<br>
<pre>

Compile with:  cc -Wall -Wextra -O2 dp.c -o dp
 
Example:

 $./dp /bin/bash
 ssh myuser@router1
 Password: ********
 ROUTER1>sh tacacs||grep -i server
 Tacacs+ Server -  public  :
            Server address: 192.168.0.99
               Server port: 49
Tacacs+ Server -  public  :
            Server address: 192.168.0.100
               Server port: 49

        
ROUTER1>exit
$exit
</pre>
-----------------------------------------------------------------------------------------------------------------------
</p>
How does this work?  

The program spawns a psuedo terminal and parses the input sent from the user looking for a "double pipe".
If a double pipe is encountered the program splits the command at the double pipe and only sends the first 
command while saving the second command in a buffer. 
When the output of the first command is returned from the remote session, the program opens a local pipe to send 
the returned output to the previously saved buffer command. 

In the example above the command "show tacacs||grep -i server", the parser splits the input into two parts:
</p>
<pre>
array[0] = "show tacacs"
array[1] = "grep -i server"
</pre>
<p>
The first command "show tacacs" is sent to the far end and the output of that command is piped locally to "grep -i server".

Tested on Ubuntu, Windows WSL Ubuntu 16.4, Windows 10/Cygwin64, Redhat 7.x, Linux Mint.

🔧 Core Stability & Safety

PTY handling reworked

Uses proper forkpty()-based architecture

Behaves like a real interactive terminal (similar to ssh, screen, tmux)

Password input no longer echoed

Local terminal runs in raw no-echo mode

Sensitive input is not intercepted or printed

Control characters handled correctly

Ctrl-C, Ctrl-Z, etc. are passed to the remote session instead of killing dp

Prevents accidental session termination

Removed temp-file state (.dp_cmd, .dp_flag)

Replaced with in-memory state machine

Eliminates race conditions and filesystem side effects

⚡ Pipeline Enhancements

Multi-stage local pipelines

show run || grep interface || wc -l

First || activates local mode

Additional || automatically map to standard pipes (|)

Streaming PTY → local pipeline

Remote output is streamed directly into local commands

Works with grep, awk, sed, wc, sort, etc.

Automatic EOF handling

Idle detection closes local filters automatically

Fixes blocking behavior with commands like:

wc -l
sort
uniq
🧠 UX Improvements

No extra Enter required

Automatic prompt refresh after pipeline execution

Eliminates need to press Return to see prompt

Consistent Enter behavior across platforms

Proper handling of \r vs \n (macOS, Linux, WSL)

Improved interactive experience

Commands behave like native shell usage

Minimal interference with normal terminal flow

🛠 New Features
📜 Logging
--log <file>

Logs remote output and internal events

Does NOT log raw keyboard input (safer for passwords)

🐞 Debug Mode
--debug

Shows:

filter start/stop events

escape mode transitions

internal state changes

⏱ Configurable Idle Timeout
--idle-ms <milliseconds>

Controls when local pipeline closes

Default ~350ms

Useful for slower devices or large outputs

🎛 Escape Command Mode (Optional)
--escape
--escape-char ctrl-]

Enter local command mode without using ||

Inspired by ssh escape behavior

Example:
Ctrl-]
local> !ls
local> resume

Commands:

resume / r → return to remote session

quit / q → exit dp

!cmd → run local shell command

help / ? → show help

🌍 Portability

Works across:

Linux (Red Hat, Ubuntu, etc.)

macOS

WSL (Windows Subsystem for Linux)

Uses standard POSIX APIs:

forkpty()

termios

poll()

ioctl()

Includes compatibility guards for:

SIGWINCH

terminal sizing

raw mode behavior

🧩 Design Improvements

Event-driven architecture

Uses poll() for responsive I/O handling

Byte-accurate I/O

No reliance on strlen() for PTY data

Proper handling of binary/control streams

Minimal input parsing

Reduces interference with:

passwords

pasted configs

control sequences

💡 Typical Usage
show run || grep interface
show log || grep ERROR || wc -l
show interfaces || awk '/up/ {print $1,$2}'
⚠️ Notes

Best used with paging disabled on network devices:

terminal length 0

Prompt detection is intentionally avoided to maintain robustness across different systems and devices

🧠 Summary

Double Pipe has evolved from a simple PTY wrapper into a:

Portable, interactive CLI augmentation tool with local processing capabilities

It enables:

Local UNIX-style pipelines on remote systems

Safe interactive usage (including passwords)

Flexible debugging and logging

Cross-platform compatibility
