#############################################################################
# Copyright (C) 2015-2017 Alibaba Group Holding Limited. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#############################################################################

#### note to vim & Makefile
# 1. set expandtab for Makefile in ~/.vimrc
#    autocmd FileType make set tabstop=4 shiftwidth=4 softtabstop=0 expandtab
# 2. use tab only at the begining of make command
#    - temp disable expandtab: set noexpandtab
#    - use tab manually
export LOCAL_INSTALL_DIR="${COW_INSTALL_DIR}"
export COW_PLUGIN_PATH="${LOCAL_INSTALL_DIR}/usr/lib/cow/"
export COW_TEST_RES_PATH="${LOCAL_INSTALL_DIR}/usr/bin/ut/res"

all:
	make -C base/src -f mmbase.mk
	make -C cow -f cow.mk
	make -C mediarecorder -f mediarecorder.mk
	make -C mediaplayer -f mediaplayer.mk
#	make -C mcv4l2 -f media_codec_v4l2.mk
	make -C test/v4l2codec -f v4l2dec_test.mk
	make -C test/cow -f cow_test.mk
#	make -C test/mediacodec -f mediacodec-test.mk

clean:
	make clean -C base/src -f mmbase.mk
	make clean -C cow -f cow.mk
	make clean -C mediarecorder -f mediarecorder.mk
	make clean -C mediaplayer -f mediaplayer.mk
#	make clean -C mcv4l2 -f media_codec_v4l2.mk
	make clean -C test/v4l2codec -f v4l2dec_test.mk
	make clean -C test/cow -f cow_test.mk
#	make clean -C test/mediacodec -f mediacodec-test.mk

install:
	mkdir -p ${LOCAL_INSTALL_DIR}/usr/lib && mkdir -p ${LOCAL_INSTALL_DIR}/usr/include
	mkdir -p ${LOCAL_INSTALL_DIR}/usr/bin && mkdir -p ${COW_PLUGIN_PATH}
	mkdir -p ${COW_TEST_RES_PATH}
	make install -C base/src -f mmbase.mk
	make install -C cow -f cow.mk
	make install -C mediarecorder -f mediarecorder.mk
	make install -C mediaplayer -f mediaplayer.mk
#	make install -C mcv4l2 -f media_codec_v4l2.mk
	make install -C cow/resource -f Local.mk
	make install -C test/v4l2codec -f v4l2dec_test.mk
	make install -C test/cow -f cow_test.mk
#	make install -C test/mediacodec -f mediacodec-test.mk

uninstall:
	rm -rf  ${LOCAL_INSTALL_DIR}/

