all : sps launch_sps
sps : sps.cpp 
	g++ -Wall -Wextra -Wpedantic -Werror -std=c++17 -O3 -o sps sps.cpp 
launch_sps : launch_sps.c
	gcc -Wall -Wextra -Wpedantic -Werror -shared -fPIC -o launch_sps.so launch_sps.c
clean :
	rm launch_sps.so sps
