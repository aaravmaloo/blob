# blob Makefile

# Compiler and flags
CC = cc
CFLAGS = -std=c11 -Wall -Wextra -pedantic
LDFLAGS =

# Detection of OS
ifeq ($(OS),Windows_NT)
    # Windows settings
    EXE = .exe
    # Use gcc on Windows usually (MinGW/MSYS2)
    CC = gcc
    # Optimization flags for Windows release
    RELEASE_FLAGS = -Os -s -ffunction-sections -fdata-sections -Wl,--gc-sections
    
    # Check if we are in a Unix-like shell (like sh.exe from MinGW)
    ifneq (,$(findstring sh.exe,$(SHELL)))
        RM = rm -f
        MKDIR = mkdir -p
    else
        RM = del /Q
        MKDIR = mkdir
    endif
else
    # Unix settings (Linux/macOS)
    EXE =
    RM = rm -f
    MKDIR = mkdir -p
    
    # Check for macOS
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        RELEASE_FLAGS = -Os -ffunction-sections -fdata-sections -Wl,-dead_strip
    else
        RELEASE_FLAGS = -Os -s -ffunction-sections -fdata-sections -Wl,--gc-sections
    endif
endif

# Targets
TARGET = blob$(EXE)
TEST_TARGET = tests$(EXE)
SRC = main.c
TEST_SRC = tests.c

.PHONY: all clean release test

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

test: $(TEST_SRC) $(SRC)
	$(CC) $(CFLAGS) $(TEST_SRC) -o $(TEST_TARGET) $(LDFLAGS)
	./$(TEST_TARGET)

release: $(SRC)
	$(CC) $(CFLAGS) $(RELEASE_FLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	-$(RM) $(TARGET) $(TEST_TARGET)
