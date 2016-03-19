CXX=g++
LD=g++

user=pi
pi=192.168.20.161
password=raspberry
dir=/home/${user}/asgard/asgard-server/

default: release

.PHONY: default release debug all clean

include make-utils/flags-pi.mk
include make-utils/cpp-utils.mk

CXX_FLAGS += -ICppSQLite -pedantic -pthread
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
	sshpass -p ${password} scp Makefile ${user}@${pi}:${dir}/
	sshpass -p ${password} scp src/*.cpp ${user}@${pi}:${dir}/src/
	sshpass -p ${password} ssh ${user}@${pi} "cd ${dir} && make"

remote_run:
	sshpass -p ${password} ssh -t ${user}@${pi} "cd ${dir} && make run"

remote_make_run:
	sshpass -p ${password} scp Makefile ${user}@${pi}:${dir}/
	sshpass -p ${password} scp src/*.cpp ${user}@${pi}:${dir}/src/
	sshpass -p ${password} ssh -t ${user}@${pi} "cd ${dir} && make && make run"

clean: base_clean

include make-utils/cpp-utils-finalize.mk
