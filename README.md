# Exam Timer

A terminal exam countdown timer with a large, `tty-clock`-inspired ncurses display.

## Install and run

### Linux

Open a terminal and install the compiler, `make`, and ncurses development files.

Ubuntu/Debian:

```sh
sudo apt update
sudo apt install gcc make libncurses-dev
```

Arch Linux:

```sh
sudo pacman -S gcc make ncurses
```

Change to the project directory, build, and run:

```sh
cd path/to/cli-timer
make
./exam-timer
```

### macOS

Open **Terminal** and install Apple's
[Command Line Tools](https://developer.apple.com/documentation/xcode/installing-the-command-line-tools)
when prompted:

```sh
xcode-select --install
```

The Command Line Tools provide the compiler and `make`; macOS provides ncurses.
After installation, build and run from Terminal:

```sh
cd path/to/cli-timer
make
./exam-timer
```

### Windows

The timer uses POSIX APIs and ncurses, so run it through
[Windows Subsystem for Linux (WSL)](https://learn.microsoft.com/windows/wsl/install).
Open **PowerShell as Administrator**, install WSL, and restart the computer if
requested:

```powershell
wsl --install
```

Open **Ubuntu** from Windows Terminal, then install the dependencies:

```sh
sudo apt update
sudo apt install gcc make libncurses-dev
```

In the same Ubuntu terminal, change to the project directory, build, and run.
Windows drives are available under `/mnt`; for example, `C:\Users\name` is
`/mnt/c/Users/name`:

```sh
cd /mnt/c/path/to/cli-timer
make
./exam-timer
```

## How to use

### Interactive setup

1. Run `./exam-timer` in the terminal for your operating system.
2. Enter the hours, minutes, and seconds. Press Enter to accept a displayed
   default. The timer asks again if all three values are `00`.
3. Enter each note and press Enter. Notes are numbered automatically.
4. On an empty note line, press Enter twice to start the countdown.
5. Use the controls below while the timer is running.

### Command-line setup

Pass the duration and an optional note when starting the program:

```sh
./exam-timer -H 1 -M 30 -S 0 -n "Database Systems Midterm"
./exam-timer -M 45 -n "Jaringan Komputer Quiz"
./exam-timer -S 5 -n "Test"
```

Hours must be non-negative; minutes and seconds must each be between 0 and 59.
Convert durations such as 90 minutes to `-H 1 -M 30`. When any duration option
is present, unspecified duration fields are zero. The total duration must be
greater than zero.

Run `./exam-timer --help` to list all options.

## Controls

- `SPACE` or `P`: pause or resume
- `R`: reset to the configured duration
- `Q`: quit

## AI acknowledgement

OpenAI Codex assisted with code revisions and documentation during the
development of this project. The project owner reviewed and directed the final
implementation.
