# Exam Timer

A terminal exam countdown timer with a large, `tty-clock`-inspired ncurses display.

## Dependencies

Ubuntu/Debian:

```sh
sudo apt install gcc make libncurses-dev
```

Arch Linux:

```sh
sudo pacman -S gcc make ncurses
```

## Compilation

```sh
make
```

## Usage

Run without arguments for interactive setup:

```sh
./exam-timer
```

Notes are numbered automatically. Press Enter after each note, then press Enter
twice on empty lines to start the timer.

Or configure it on the command line:

```sh
./exam-timer -H 1 -M 30 -S 0 -n "Database Systems Midterm"
./exam-timer -M 45 -n "Jaringan Komputer Quiz"
./exam-timer -S 5 -n "Test"
```

Hours must be non-negative; minutes and seconds must each be between 0 and 59.
Convert durations such as 90 minutes to `-H 1 -M 30`.
When any duration option is present, unspecified duration fields are zero.

## Controls

- `SPACE` or `P`: pause/resume
- `R`: reset to the configured duration
- `Q`: quit

Run `./exam-timer --help` for all options.
