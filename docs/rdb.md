## Setting up VSCode
The `docs/rdb-vscode-example/` directory contains example configurations that
you can place in `.vscode/`. There is one example C/C++ and Launch
configuration for Linux/WSL and one for MSYS2. Replace the `miDebuggerPath`
value for your configuration in `.vscode/launch.json` with the path to your own
GDB binary targeting MIPS. You can use any GDB build of your choice, but
mips64-ultra-elf-gdb is recommended because it includes a patch that allows the
stub to specify loaded and unloaded libraries directly in the stop reply.
Ordinarily GDB requires the stub to first report that it stopped because of a
library change, and then transfer the entire list of loaded libraries in an XML
document, which incurs a lot of data transfers and round trip latencies. This
causes a very disruptive stutter every time an overlay is loaded or unloaded,
which severely degrades the debugging experience. With the patch included with
mips64-ultra-elf-gdb, the stutter is more acceptable.

## Building
You can build the project directly from VSCode by running the
`Make (Linux GCC)` task. This build task should work for MSYS2 as well.

## Connecting to Everdrive
Everdrive v3 and X7 are both supported, but Everdrive OS v3.01 or newer is
required. Copy `zelda_ocarina_mq_dbg.z64` to your Everdrive SD card. Connect
the USB cable from your Everdrive to your PC. If you're using WSL1, you
should be able to access the USB Serial Port at `/dev/ttySN`, where `N` is the
number of the COM port. Check Windows Device Manager to see what COM port your
serial device is on, it should appear under Ports (COM & LPT) as
`USB Serial Port (COMN)`, where `N` is the number of the port. If you're using
WSL2, some extra steps are needed to get access to the serial port in the WSL
instance, see [here](https://docs.microsoft.com/en-us/windows/wsl/connect-usb).
After attaching the USB device, the serial port should appear at
`/dev/ttyUSBN`, where `N` is usually `0`. Before you can access it you have to
change the permissions; `sudo chmod 0666 /dev/ttyUSBN`. On Linux the port
should appear at `/dev/ttyUSBN` after the device is plugged in, where `N` is a
number. Do `ls /dev` to see what number your device is on.

In `.vscode/launch.json`, change the `miDebuggerServerAddress` value of the
configuration you'll be using to the path to your serial device (or just `COMN`
on MSYS2).

## Connecting to Project64
Download the Homeboy fork of Project64 from
[here](https://github.com/glankk/project64/releases). If you already have a
recent version of Project64, it's enough to copy the `Project64.exe` executable
from the homeboy fork into your existing Project64 directory, or place it next
to your existing Project64 executable with a different filename. Start
Project64 and open the Configuration dialog. Under
`General Settings -> Advanced`, check `Always use interpreter core`. Under
`General Settings -> Homeboy`, check `Enable Homeboy` and
`Enable FIFO over TCP`.

In `.vscode/launch.json`, change the `miDebuggerServerAddress` value of the
configuration you'll be using to `tcp:IP:PORT`, where `PORT` is the TCP port
configured in Project64 under `General Settings -> Homeboy` (default `42764`),
and `IP` is the IP address to connect to. For WSL1 and MSYS2, the `IP` part can
be empty. For WSL2, use the IP address of the WSL NAT adapter. This address can
be found by opening a Command Prompt and typing `ipconfig /all`. The IP address
will be listed next to `IPv4 Address` under `Ethernet adapter vEthernet (WSL)`.

## Running
Start `zelda_ocarina_mq_dbg.z64` in Project64 or on the Everdrive. Run your
launch configuration in VSCode. When the stub first receives a packet from GDB,
it will attach the debugger and halt the game. When you first connect, or when
you press "Pause" in VSCode while the game is running, you will most likely
find yourself paused at `osRecvMesg`, called from `Graph_TaskSet00` on the
graph thread. If this is the case, the graph thread might think that RDP has
hung up because of the delay caused by the thread being halted (the
`osSetTimer` timer is still running while the thread is paused). Use "Step
out". If `msg` (seen in the Locals view) is `0x29A`, the timer has expired and
the graph thread will try to invoke the fault manager by crashing. Edit `msg`
in the Locals view and set it to `0`, this will undo the timeout and make the
graph thread continue normally when you resume the game.

From here on, you can set breakpoints, inspect values, edit values, and
continue running. When the game is running, the debugger will respond to
interrupt requests when pressing "Pause" in VSCode. Breakpoints can be set
while the game is running, and they can be set in overlays before they are
loaded. Breakpoints in unloaded overlays will be placed as soon as the stub
reports that the overlay has been loaded, and this can be used to stop when the
game hits an actor init function, for example.

The debugger thread uses certain OS functions that you might run in to on
threads that you are debugging (primarily `osSendMesg` and `osRecvMesg`).
Stepping or setting breakpoints inside these functions is not allowed, because
doing so would result in the debugger thread itself hitting those breakpoints,
causing the system to hang. If you try to step into such a function, the
debugger will simply step over the function call. If you pause a thread in an
OS function that the debugger will not allow you to step inside, use "Step
out", or set a breakpoint on the nearest stack frame that is outside OS
code and press "Continue".

Certain threads are necessary for the debugger thread to function (`viThread`,
`piThread`, and `sIdleThread`). These threads are exempt from debugging, and
will not appear in the thread list. Setting breakpoints or stepping on code
executed by these threads will cause the system to hang.

Pressing "Stop debugging" in VSCode, or disconnecting the USB cable, will cause
the debugger thread to detach and the game to continue running normally. Note
that the usual fault manager is not present, even when the debugger is
detached.
