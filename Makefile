user=pi
pi=192.168.20.161
password=raspberry
dir=/home/${user}/asgard/asgard-server/

default: release

.PHONY: default release debug all clean

include make-utils/flags-pi.mk
include make-utils/cpp-utils.mk

CXX_FLAGS += -pedantic -pthread
LD_FLAGS  += -llirc_client -lwiringPi -pthread

$(eval $(call auto_folder_compile,src))
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
	sshpass -p ${password} scp Makefile ${user}@${pi}:${dir}/
	sshpass -p ${password} scp src/*.cpp ${user}@${pi}:${dir}/
	sshpass -p ${password} ssh ${user}@${pi} "cd ${dir} && make"

remote_run:
	sshpass -p ${password} ssh ${user}@${pi} "cd ${dir} && make run"

clean: base_clean

include make-utils/cpp-utils-finalize.mk
