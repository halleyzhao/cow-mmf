LOCAL_DEFINES=-DMM_LOG_OUTPUT_E -DMM_LOG_OUTPUT_W -DMM_LOG_OUTPUT_I -Wall
LOCAL_INCLUDES=-I../../../../include/ -I../../../../src/base/include/ -I../../include/
LOCAL_SHARED=-fPIC -shared

all:libcowplayer.so
clean:
	rm -rf libcowplayer.so

libcowplayer.so:
	g++ ${LOCAL_SHARED} ${LOCAL_DEFINES} ${LOCAL_INCLUDES} \
		../../src/media_attr_str.cc \
		../../src/media_meta.cc \
		../../src/media_buffer.cc \
		../../src/player/pipeline_player.cc \
		../../src/player/cowplayer.cc \
		-o libcowplayer.so
