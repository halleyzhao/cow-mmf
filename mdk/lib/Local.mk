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

all:

clean:

install:
	install -m 755  libmmbase.so -D $(LOCAL_INSTALL_DIR)/usr/lib/libmmbase.so
	install -m 755  libcowbase.so -D $(LOCAL_INSTALL_DIR)/usr/lib/libcowbase.so
	install -m 755  libmediaplayer.so -D $(LOCAL_INSTALL_DIR)/usr/lib/libmediaplayer.so
	install -m 755  libcowplayer.so -D $(LOCAL_INSTALL_DIR)/usr/lib/libcowplayer.so
	install -m 755  libcow-avhelper.so -D $(LOCAL_INSTALL_DIR)/usr/lib/libcow-avhelper.so
	install -m 755  cow/* -D $(LOCAL_INSTALL_DIR)/usr/lib/cow

