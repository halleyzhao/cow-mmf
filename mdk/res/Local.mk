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

all:${cow_plugins.xml}

clean:

install:
	sed "s#/usr/lib/cow/#${COW_PLUGIN_PATH}#g" cow_plugins.xml >${COW_PLUGIN_PATH}/cow_plugins.xml
	sed "s#/usr/lib/cow/#${COW_PLUGIN_PATH}#g" cow_plugins_ubt.xml >${COW_PLUGIN_PATH}/cow_plugins_ubt.xml
	install -m 755  h264.h264 -D $(COW_TEST_RES_PATH)/h264.h264

