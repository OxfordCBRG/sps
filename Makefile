all: sps ccbspank
sps : sps.cpp 
	g++ -O3 -Wall -std=c++17 -o sps sps.cpp 
ccbspank : ccbspank.c
	gcc -shared -fPIC -o ccbspank.so ccbspank.c
