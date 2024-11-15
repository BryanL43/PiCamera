all:
	sudo g++ brains.c camera/camera.cpp -o waymore -Wall -g -I./camera -D \
	USE_BCM2835_LIB -std=c++11 -lbcm2835 -lm -lstdc++

run:
	sudo g++ brains.c camera/camera.cpp -o waymore -Wall -g -I./camera -D \
	USE_BCM2835_LIB -std=c++11 -lbcm2835 -lm -lstdc++
	sudo ./waymore

clean:
	sudo rm -f waymore
