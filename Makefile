all : sps launch_sps
sps : sps.cpp 
	g++ -O3 -Wall -std=c++17 -o sps sps.cpp 
launch_sps : launch_sps.c
	gcc -shared -fPIC -o launch_sps.so launch_sps.c
clean :
	rm launch_sps.so sps
