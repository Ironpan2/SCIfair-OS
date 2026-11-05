# SciFair-OS

A tiny 32-bit x86 operating system written from scratch in C, built for a science fair experiment:

> **How much slower does a program get when it has to show its work as text, or in a graphical user interface (GUI)?**

The OS boots on its own with no Linux or Windows underneath. It has its own keyboard driver, timer, text console, graphics driver and command shell. Its "programs" are functions built into the kernel, and three of them calculate pi in different ways so you can compare them.

---

## Quick start

### 1. Install the tools (one time)

You need `gcc`, `make` and the QEMU emulator. On Void Linux:

```sh
sudo xbps-install -S qemu-system-i386
```

(Debian/Ubuntu: `sudo apt install build-essential qemu-system-x86`)

### 2. Build and run

```sh
make          # builds build/scifair-os.elf
make run      # boots it in a QEMU window
```

Type `help` in the QEMU window to see the commands. The OS also prints everything to the terminal you ran `make run` from, and you can type commands there too.

### 3. Run the experiment

```
bench
```

---

## Commands

| Command | What it does |
|---|---|
| `hello` | Says hello in rainbow colors |
| `echo <text>` | Repeats what you type |
| `fib [n]` | First n Fibonacci numbers |
| `primes [n]` | Prime numbers up to n (Sieve of Eratosthenes) |
| `colors` | Shows the 16 text colors |
| **`pitext [digits]`** | **Calculates pi and prints every digit as text** |
| **`pigui [digits]`** | **Calculates pi in a graphical window** with a progress bar, a rainbow progress ring and live status |
| **`pibase [digits]`** | **Calculates pi without displaying anything while timing** (the control group) |
| **`bench [digits] [trials]`** | **Runs the full experiment and compares all three** (defaults: 1000 digits, 3 trials) |
| `results [clear]` | Lists every run since boot |
| `graph` | Shows a bar chart of the last `bench` |
| `time <command>` | Times any command, e.g. `time primes 5000` |
| `sysinfo` / `uptime` / `about` | System information |
| `clear`, `help`, `reboot`, `shutdown` | The usual |

Use the up and down arrow keys to bring back previous commands.

---

## The experiment

### What's being compared

All three pi programs run **exactly the same calculation**, the Rabinowitz–Wagon "spigot" algorithm, which produces pi one digit at a time using only whole numbers. The only difference is what happens when each digit is ready:

| Mode | What happens to each digit |
|---|---|
| **No display** (`pibase`) | Saved in memory. Nothing else. This is the **control**. |
| **Text** (`pitext`) | Also written to the 80×25 text screen. |
| **GUI** (`pigui`) | Drawn into a 320×200, 256-color window. The progress bar, progress ring, percentage and status text are updated, and then the whole frame is copied to the screen. |

That means:
- **Text time − No-display time** = the cost of showing text
- **GUI time − No-display time** = the cost of the graphical interface

### Variables

- **Independent variable:** the display mode (none / text / GUI)
- **Dependent variable:** time to calculate and display the digits, in milliseconds, measured with the CPU's cycle counter (the TSC). The TSC counts every clock tick, so it's precise to well under a microsecond.
- **Controlled variables:** the same algorithm, the same number of digits, the same computer and the same OS. Every run must produce the same digits, which the OS checks with a checksum.

### How the OS keeps it a fair test

`bench` does these automatically:

1. **Warm-up:** each mode runs once first without being counted. Code is slower the first time it runs ("cold" caches).
2. **CPU settling:** before every timed run, the CPU is kept busy for 250 ms. Modern CPUs slow down to save power when they're idle and take a moment to speed back up. During testing, this made some "no display" runs look almost **twice as slow** as others, and even made text look faster than doing nothing!
3. **Counterbalancing:** the order rotates each trial (none→text→GUI, then text→GUI→none, then GUI→none→text) so no mode always goes first.
4. **Repeated trials:** you get the average, fastest and slowest of each mode.
5. **Correctness check:** every run must produce the same digits.
6. Switching the screen into graphics mode is **not** timed. Drawing the window **is** timed, because drawing is part of what a GUI program has to do.

### Example results

From one run in QEMU (emulated CPU) with `bench 1000 5`. **Your numbers will be different**, and that's expected:

| Display mode | Average | vs. no display |
|---|---|---|
| No display | 13.2 ms | 1.00× |
| Text console | 14.2 ms | 1.06× (+6.9%) |
| Graphical GUI | 94.1 ms | 7.10× (+610%) |

### Saving your data

```sh
make log
```

This runs the OS and saves everything sent to the serial port in `results.txt`. That includes a CSV line for every trial:

```
TRIAL,trial,mode,digits,time_us,cycles,checksum
TRIAL,1,none,1000,13213,31970625,3de0e3b4
...
SUMMARY,none,1000,5,13246,13079,13467      <- mode,digits,trials,avg_us,min_us,max_us
```

Copy the `TRIAL` and `SUMMARY` lines into a spreadsheet to make your own graphs. Times are in microseconds (1000 µs = 1 ms).

### Ideas for going further

- **Change the number of digits:** try `bench 100`, `bench 1000` and `bench 5000`. The maths takes about 4× longer each time the digit count doubles, but the display work only doubles. What happens to the GUI's *percentage* slowdown as the digit count grows? Why?
- **Emulated vs. real CPU:** `make run-kvm` runs the OS directly on your real processor instead of an emulated one. (You need permission to use `/dev/kvm`: `sudo usermod -aG kvm $USER`, then log out and back in.) Are the ratios the same?
- **Real hardware:** `make iso` builds a bootable image (needs `xorriso`; install with `sudo xbps-install xorriso`). You can write it to a USB stick and boot an old PC that supports legacy BIOS.

---

## How the OS works (file guide)

| File | What it does |
|---|---|
| `src/boot.s` | First code that runs. The "multiboot header" lets GRUB/QEMU find and load the kernel, then it sets up a stack and calls `kmain`. |
| `src/kernel.c` | `kmain()`: starts each part of the OS, then the shell. |
| `src/cpu.c`, `src/isr.s` | CPU tables (GDT, IDT), the interrupt controller (PIC) and the crash screen. |
| `src/timer.c` | 1000 Hz timer and cycle-counter calibration. |
| `src/keyboard.c` | Turns key presses into characters. |
| `src/terminal.c` | Text screen: writes characters to video memory at `0xB8000`. |
| `src/gfx.c` | Graphics mode: programs the VGA card's registers directly, and provides drawing functions (rectangles, gradients, circles, text). |
| `src/serial.c` | Serial port, used for saving results. |
| `src/lib.c` | Our own mini C library (`printf`, `memcpy`, …). The normal one needs an OS to run on! |
| `src/pi_core.c` | The pi algorithm, shared by all three modes. |
| `src/pi.c` | Runs the three modes and does the timing. |
| `src/pi_gui.c` | The GUI window for `pigui`. |
| `src/bench.c` | The `bench`, `results` and `graph` commands. |
| `src/shell.c` | The command line and every other command. |
| `tests/` | `make test` checks the pi algorithm against Python, up to 10,000 digits. |
