#### note to vim & Makefile
# 1. set expandtab for Makefile in ~/.vimrc
#    autocmd FileType make set tabstop=4 shiftwidth=4 softtabstop=0 expandtab
# 2. use tab only at the begining of make command
#    - temp disable expandtab: set noexpandtab
#    - use tab manually
export RELEASE_VERSION="0.0.1:20161108"
export MY_CURRENT_PATH=$(CURDIR)
export LOCAL_INSTALL_DIR="$(MY_CURRENT_PATH)/fakeroot"
export COW_PLUGIN_PATH="${LOCAL_INSTALL_DIR}/usr/lib/cow/"
export COW_TEST_RES_PATH="${LOCAL_INSTALL_DIR}/usr/bin/ut/res"

all:
	export MDK_PATH=$(MY_CURRENT_PATH)
	export LD_LIBRARY_PATH="${MDK_PATH}/lib:"${LD_LIBRARY_PATH}
	make -C example -f example.mk

clean:
	make clean -C example -f example.mk

install:
	mkdir -p ${LOCAL_INSTALL_DIR}/usr/lib && mkdir -p ${LOCAL_INSTALL_DIR}/usr/include
	mkdir -p ${LOCAL_INSTALL_DIR}/usr/bin && mkdir -p ${COW_PLUGIN_PATH}
	make install -C bin -f Local.mk
	make install -C lib -f Local.mk
	make install -C res -f Local.mk
	make install -C example -f example.mk
uninstall:
	rm -rf  ${LOCAL_INSTALL_DIR}/

