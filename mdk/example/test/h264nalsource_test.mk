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

CC := g++
CPPFLAGS := -c -g -fPIC -Wall -Wno-multichar -fpermissive
CPPFLAGS += -D_COW_SOURCE_VERSION=\"$(RELEASE_VERSION)\"
INCLUDES :=  -I../../include                 \
             -I../../include/multimedia      \
             -I../include
OUT_PATH = ../../out/
INSTALL_PATH = $(LOCAL_INSTALL_DIR)

LIBDIRS := -L../../lib -L$(OUT_PATH)/usr/lib
CPPLDFLAGS  := -lpthread -z defs
CPPLDFLAGS += `pkg-config --cflags --libs libavformat libavcodec libavutil`
CPPLDFLAGS += -lmmbase -lcowbase -lh264NalSource
OBJS = h264Nalsource_test.o
TARGET = h264Nalsource-test


$(TARGET):$(OBJS)
	@(test -d $(OUT_PATH)/usr/bin || mkdir -p $(OUT_PATH)/usr/bin)
	${CC} ${OBJS} -o $(OUT_PATH)/usr/bin/$@  ${LIBDIRS} ${CPPLDFLAGS}

%.o : %.cc
	${CC}  ${CPPFLAGS}  ${INCLUDES}  $<

clean:
	rm -f $(OBJS)
	rm -f $(OUT_PATH)/usr/bin/$(TARGET)

install: $(OUT_PATH)/usr/bin/$(TARGET)
	test -d $(INSTALL_PATH)/usr/bin || mkdir -p $(INSTALL_PATH)/usr/bin
	install -m 755  $(OUT_PATH)/usr/bin/$(TARGET) -D $(INSTALL_PATH)/usr/bin/$(TARGET)

