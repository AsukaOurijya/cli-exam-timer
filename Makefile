CC := gcc
CFLAGS := -Wall -Wextra -Wpedantic -std=c11
LDLIBS := -lncurses
TARGET := exam-timer
SOURCES := src/main.c src/timer.c src/display.c

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SOURCES) src/timer.h src/display.h
	$(CC) $(CFLAGS) $(SOURCES) -o $@ $(LDLIBS)

clean:
	$(RM) $(TARGET) tests/timer_test

test: $(TARGET)
	$(CC) $(CFLAGS) -Isrc tests/timer_test.c src/timer.c -o tests/timer_test
	./tests/timer_test
	./$(TARGET) --help >/dev/null
	! ./$(TARGET) -M 60 >/dev/null 2>&1
	! ./$(TARGET) -H 0 -M 0 -S 0 >/dev/null 2>&1
	output="$$(printf '\n\n\n0\n1\n0\n' | ./$(TARGET) 2>&1 || true)"; test "$$(printf '%s' "$$output" | grep -c 'Hours \[Default=00\]')" -eq 2 && printf '%s' "$$output" | grep -q 'Total duration must be greater than zero.'
	output="$$(printf '\n\n1\nx\n' | ./$(TARGET) 2>&1 || true)"; printf '%s' "$$output" | grep -q 'Would you like to add additional notes? (y/n)?' && printf '%s' "$$output" | grep -q 'Please enter y or n.'
