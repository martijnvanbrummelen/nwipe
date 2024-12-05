# Makefile für prng_analysis

# Compiler
CXX = g++

# Compiler-Flags
CXXFLAGS = -std=c++17 -Wall -Wextra -O2

# Zielname
TARGET = prng_analysis

# Quellcodedateien
SRCS = prng_analysis.cpp

# Objektdateien
OBJS = $(SRCS:.cpp=.o)

# Standardziel
all: $(TARGET)

# Linken des Zielprogramms
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

# Kompilieren der Objektdateien
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Bereinigen der generierten Dateien
clean:
	rm -f $(TARGET) $(OBJS)

# Phony-Ziele
.PHONY: all clean
