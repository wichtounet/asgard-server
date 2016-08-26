CXX=g++
LD=g++

default: release

include make-utils/flags-pi.mk
include make-utils/cpp-utils.mk

pi.conf:
	echo "user=pi" > pi.conf
	echo "pi=192.168.20.161" >> pi.conf
	echo "password=raspberry" >> pi.conf
	echo "dir=/home/pi/asgard/asgard-server/" >> pi.conf

conf: pi.conf

include pi.conf

CXX_FLAGS += -ICppSQLite -pedantic -pthread -Include -Iasgard-lib/include
LD_FLAGS  += -lsqlite3 -lmongoose

ifeq (,$(MAKE_NO_RPI))
LD_FLAGS  += -llirc_client -lwiringPi
endif

$(eval $(call auto_folder_compile,src))
$(eval $(call auto_folder_compile,CppSQLite))
$(eval $(call auto_add_executable,server))

release: release_server
release_debug: release_debug_server
debug: debug_server

all: release release_debug debug

run: release
	sudo ./release/bin/server

remote_clean:
	sshpass -p ${password} ssh ${user}@${pi} "cd ${dir} && make clean"

remote_make:
	sshpass -p ${password} scp -p Makefile ${user}@${pi}:${dir}/
	sshpass -p ${password} scp -p src/*.cpp ${user}@${pi}:${dir}/src/
	sshpass -p ${password} scp -p include/*.hpp ${user}@${pi}:${dir}/include/
	sshpass -p ${password} scp -p asgard-lib/include/asgard/*.hpp ${user}@${pi}:${dir}/asgard-lib/include/asgard/
	sshpass -p ${password} ssh -t ${user}@${pi} "cd ${dir} && make -j4"

remote_run:
	sshpass -p ${password} ssh -t ${user}@${pi} "cd ${dir} && make run"

remote_make_run:
	sshpass -p ${password} scp -p Makefile ${user}@${pi}:${dir}/
	sshpass -p ${password} scp -p src/*.cpp ${user}@${pi}:${dir}/src/
	sshpass -p ${password} scp -p include/*.hpp ${user}@${pi}:${dir}/include/
	sshpass -p ${password} scp -p asgard-lib/include/asgard/*.hpp ${user}@${pi}:${dir}/asgard-lib/include/asgard/
	sshpass -p ${password} ssh -t ${user}@${pi} "cd ${dir} && make -j4 && make run"

clean: base_clean

include make-utils/cpp-utils-finalize.mk

.PHONY: default release_debug release debug all clean conf
