default: release

.PHONY: default release debug all clean

include make-utils/flags-pi.mk
include make-utils/cpp-utils.mk

CXX_FLAGS += -pedantic
LD_FLAGS  += -llirc_client

$(eval $(call auto_folder_compile,src))
$(eval $(call auto_add_executable,server))

release: release_server
release_debug: release_debug_server
debug: debug_server

all: release release_debug debug

run: release
	./release/bin/server

clean: base_clean

include make-utils/cpp-utils-finalize.mk
