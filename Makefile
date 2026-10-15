CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -Wpedantic -g -O2 -MMD -MP
SRC_DIR := src
TEST_DIR:= tests
BUILD_DIR := build

# `make test SANITIZE=1` builds everything with AddressSanitizer and
# UndefinedBehaviorSanitizer into a separate directory
ifeq ($(SANITIZE),1)
CFLAGS  += -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
BUILD_DIR := build-sanitize
endif

# All .c files in src/ (library code only, no main.c here)
SRC     := $(wildcard $(SRC_DIR)/*.c)
OBJS    := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRC))

# Main program (main.c at project root)
MAIN_SRC := main.c
TARGET   := $(BUILD_DIR)/main

# Test programs (every tests/*.c becomes its own binary)
TEST_SRCS := $(wildcard $(TEST_DIR)/*.c)
TEST_BINS := $(patsubst $(TEST_DIR)/%.c,$(BUILD_DIR)/%,$(TEST_SRCS))

# The b+tree test is also built against a library with a tiny tree order,
# giving deep trees that exercise splits and merges on every level
SMALL_DIR  := $(BUILD_DIR)/small
SMALL_DEFS := -DBPT_MAX_CHILDREN=4 -DBPT_MAX_ENTRIES_LEAF=3
SMALL_OBJS := $(patsubst $(SRC_DIR)/%.c,$(SMALL_DIR)/%.o,$(SRC))
TEST_BINS  += $(BUILD_DIR)/bpt_small_test

.PHONY: all run clean test

all: $(TARGET)

# Build main program
$(TARGET): $(MAIN_SRC) $(OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^

# Compile object files from src/
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(SMALL_DIR)/%.o: $(SRC_DIR)/%.c | $(SMALL_DIR)
	$(CC) $(CFLAGS) $(SMALL_DEFS) -c -o $@ $<

# Build each test binary (link with library objects, but not main.c)
$(BUILD_DIR)/%: $(TEST_DIR)/%.c $(OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR)/bpt_small_test: $(TEST_DIR)/bpt_test.c $(SMALL_OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SMALL_DEFS) -o $@ $^

# Create build dirs if missing
$(BUILD_DIR) $(SMALL_DIR):
	mkdir -p $@

# Run the main program
run: $(TARGET)
	mkdir -p db
	./$(TARGET)

# Run all tests, or a specific test if TEST is set (e.g. make test TEST=bpt_test)
test: $(TEST_BINS)
	@failures=0; \
	if [ -n "$(TEST)" ]; then \
		tests="$(BUILD_DIR)/$(TEST)"; \
	else \
		tests="$(TEST_BINS)"; \
	fi; \
	for t in $$tests; do \
		echo "Running $$t..."; \
		TEST_DB_DIR=$(BUILD_DIR)/testdb ./$$t || failures=$$((failures+1)); \
	done; \
	if [ $$failures -eq 0 ]; then \
		echo "All tests passed."; \
	else \
		echo "$$failures test(s) failed."; \
		exit 1; \
	fi

clean:
	rm -rf build build-sanitize

-include $(OBJS:.o=.d) $(SMALL_OBJS:.o=.d) $(TEST_BINS:=.d) $(TARGET).d
