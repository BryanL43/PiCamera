# Compiler settings & libraries
CC = gcc
CXX = g++
CFLAGS = -Wall -g -I./camera -I/usr/include/libcamera -I/usr/include/opencv4 # -D USE_BCM2835_LIB (for main car module)
CXXFLAGS = -Wall -g -I/usr/include/libcamera -I/usr/include/opencv4 -std=c++17
LIBS = -lstdc++ -lcamera -lcamera-base -lopencv_core -lopencv_imgcodecs -lopencv_imgproc -lopencv_highgui # -lbcm2835 -lm (for main car module)

# Directories
OUTDIR = out
SRCDIR = camera

# Find all C++ source files in the camera directory
CPP_FILES = $(wildcard $(SRCDIR)/*.cpp)
OBJECTS = $(patsubst $(SRCDIR)/%.cpp, $(OUTDIR)/%.o, $(CPP_FILES))

# Object files & output file name
TARGET = waymore

# Default target
all: $(TARGET)

# Build target
$(TARGET): $(OBJECTS) $(OUTDIR)/brains.o
	$(CXX) $(OBJECTS) $(OUTDIR)/brains.o -o $(TARGET) $(LIBS)

# Compile C++ files
$(OUTDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(OUTDIR)
	$(CXX) -c $< -o $@ $(CXXFLAGS)

# Compile C file
$(OUTDIR)/brains.o: brains.c
	@mkdir -p $(OUTDIR)
	$(CC) -c brains.c -o $(OUTDIR)/brains.o $(CFLAGS)

# Run target
run: $(TARGET)
	./$(TARGET)

# Clean target
clean:
	rm -rf $(TARGET) $(OUTDIR)

.PHONY: all run clean
