CC      := gcc
CFLAGS  := -Wall -Wextra -g
SRC_DIR := src
TEST_DIR:= tests
BUILD_DIR := build

# All .c files in src/ (library code only, no main.c here)
SRC     := $(wildcard $(SRC_DIR)/*.c)
OBJS    := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRC))

# Main program (main.c at project root)
MAIN_SRC := main.c
TARGET   := $(BUILD_DIR)/main

# Test programs (every tests/*.c becomes its own binary)
TEST_SRCS := $(wildcard $(TEST_DIR)/*.c)
TEST_BINS := $(patsubst $(TEST_DIR)/%.c,$(BUILD_DIR)/%,$(TEST_SRCS))

.PHONY: all run clean test

all: $(TARGET)

# Build main program
$(TARGET): $(MAIN_SRC) $(OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^

# Compile object files from src/
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# Build each test binary (link with library objects, but not main.c)
$(BUILD_DIR)/%: $(TEST_DIR)/%.c $(OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ 

# Create build dir if missing
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Run the main program
run: $(TARGET)
	./$(TARGET)

# Run all tests, or a specific test if TEST is set
test: $(TEST_BINS)
	@failures=0; \
	if [ -n "$(TEST)" ]; then \
		tests="$(BUILD_DIR)/$(TEST)"; \
	else \
		tests="$(TEST_BINS)"; \
	fi; \
	for t in $$tests; do \
		echo "Running $$t..."; \
		./$$t || failures=$$((failures+1)); \
	done; \
	if [ $$failures -eq 0 ]; then \
		echo "All tests passed."; \
	else \
		echo "$$failures test(s) failed."; \
		exit 1; \
	fi

clean:
	rm -rf $(BUILD_DIR)
