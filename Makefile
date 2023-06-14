version = 3.4
release = 2
all : sps launch_sps
sps : sps.cpp 
	g++ -Wall -Wextra -Wpedantic -Werror -std=c++17 -O3 -o sps sps.cpp 
launch_sps : launch_sps.c
	gcc -Wall -Wextra -Wpedantic -shared -fPIC -o launch_sps.so launch_sps.c
clean :
	rm -rf launch_sps.so sps sps_$(version)-$(release) sps_$(version)-$(release).deb
fake_install :
	install -m 644 -D LICENSE $(prefix)/usr/share/doc/sps/LICENSE
	install -m 644 -D README.md $(prefix)/usr/share/doc/sps/README.md
	install -m 755 -D ckill $(prefix)/usr/bin/ckill
	install -m 755 -D launch_sps.so $(prefix)/usr/lib64/slurm/launch_sps.so
	install -m 755 -D process_sps.sh $(prefix)/etc/slurm/process_sps.sh
	install -m 755 -D sps $(prefix)/usr/bin/sps
	install -m 755 -D sps-plot $(prefix)/usr/bin/sps-plot
	install -m 755 -D sps-stop $(prefix)/usr/bin/sps-stop
	install -m 755 -D sps-sum $(prefix)/usr/bin/sps-sum
install : prefix = /
install : fake_install
deb : prefix = ./sps_$(version)-$(release)/
deb : all fake_install
	sed -i 's/Version: .*/Version: $(version)-$(release)/' DEBIAN/control
	cp -ar DEBIAN sps_$(version)-$(release)
	dpkg-deb --build --root-owner-group sps_$(version)-$(release)
