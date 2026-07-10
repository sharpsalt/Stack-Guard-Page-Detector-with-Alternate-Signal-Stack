# Compiler settings
CC = gcc

# Compiler flags:
# -Wall -Wextra: Enable extensive warning messages
# -O0: Disable optimizations to prevent tail-call optimization of our recursion
# -g: Include debugging information
# -I./include: Add the include directory to the include path for header files
# -pthread: Required for POSIX threads integration
CFLAGS = -Wall -Wextra -O0 -g -I./include -pthread

# Directories
SRC_DIR = src
OBJ_DIR = obj
INC_DIR = include

# Source files and object files
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

# The target executable
TARGET = detector

# Default target
all: $(TARGET)

# Rule to link the object files and create the target executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Rule to compile source files into object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Ensure the object directory exists before trying to place files in it
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Rule to clean up the generated files
clean:
	rm -rf $(OBJ_DIR) $(TARGET)