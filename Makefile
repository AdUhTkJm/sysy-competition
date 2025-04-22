CXX = clang++
CXXFLAGS = -std=c++17 -Wall -g

SRC_DIR = src
OBJ_DIR = build
BIN_DIR = bin
TARGET = $(BIN_DIR)/sysc

# Find all source files in src directory (including subdirectories)
SOURCES = $(wildcard $(SRC_DIR)/**/*cpp)
OBJECTS = $(SOURCES:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

# Create the bin and obj directories
$(shell mkdir -p $(BIN_DIR))

# The target to build
$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $(TARGET)

# Rule for compiling source files into object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up build files
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
