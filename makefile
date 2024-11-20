# Compiler settings & libraries
CC = gcc
CXX = g++
CFLAGS = -Wall -g -I./camera -I/usr/include/libcamera -I/usr/include/opencv4 -D USE_BCM2835_LIB
CXXFLAGS = -Wall -g -I/usr/include/libcamera -I/usr/include/opencv4 -std=c++17
LIBS = -lbcm2835 -lm -lstdc++ -lcamera -lcamera-base -lopencv_core -lopencv_imgcodecs -lopencv_imgproc -lopencv_highgui

# Directories
OUTDIR = out

# Object files & output file name
OBJECTS = $(OUTDIR)/brains.o $(OUTDIR)/camera.o
TARGET = waymore

# Default target
all: $(TARGET)

# Build target
$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $(TARGET) $(LIBS)

# Compile C file
$(OUTDIR)/brains.o: brains.c
	@mkdir -p $(OUTDIR)
	$(CC) -c brains.c -o $(OUTDIR)/brains.o $(CFLAGS)

# Compile C++ file
$(OUTDIR)/camera.o: camera/camera.cpp
	@mkdir -p $(OUTDIR)
	$(CXX) -c camera/camera.cpp -o $(OUTDIR)/camera.o $(CXXFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(TARGET) $(OUTDIR)

.PHONY: all run clean
