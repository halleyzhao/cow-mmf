#!/bin/bash

export MM_BUILD_OUT_LIB_PATH="base/build/out/rootfs/usr/lib"
export MM_BUILD_OUT_BIN_PATH="base/build/out/rootfs/usr/bin"
MDK_PATH=$(pwd)
echo "MDK_PATH= $MDK_PATH"

cd ..
echo "make multimedia"
make -j4
echo "copy mm libs"


cd $MDK_PATH/../$MM_BUILD_OUT_BIN_PATH
mkdir -p $MDK_PATH/bin
install -m 755 cowplayer-test  -D $MDK_PATH/bin/cowplayer-test

cd $MDK_PATH/../$MM_BUILD_OUT_LIB_PATH
mkdir -p $MDK_PATH/lib/cow
install -m 755  libmmbase.so -D $MDK_PATH/lib/libmmbase.so
install -m 755  libcowbase.so -D $MDK_PATH/lib/libcowbase.so
install -m 755  libmediaplayer.so -D $MDK_PATH/lib/libmediaplayer.so
install -m 755  libcowplayer.so -D $MDK_PATH/lib/libcowplayer.so
install -m 755  libcow-avhelper.so -D $MDK_PATH/lib/libcow-avhelper.so
install -m 755  cow/* -D $MDK_PATH/lib/cow/

## strip binary symbols
for my_binary in "$MDK_PATH/lib/*.so"; do
    strip ${my_binary}
done

for my_binary in "$MDK_PATH/lib/cow/*.so"; do
    strip ${my_binary}
done

strip $MDK_PATH/bin/cowplayer-test


echo "copy cow_plugins.xml"
cd $MDK_PATH/../cow/resource
install -m 755 cow_plugins.xml -D $MDK_PATH/res/cow_plugins.xml
install -m 755 cow_plugins_ubt.xml -D $MDK_PATH/res/cow_plugins_ubt.xml

echo "copy mm header files"
cd $MDK_PATH/..
mkdir -p $MDK_PATH/include/multimedia

install -m 755  base/include/multimedia/codec.h -D $MDK_PATH/include/multimedia/codec.h
install -m 755  base/include/multimedia/elapsedtimer.h -D $MDK_PATH/include/multimedia/elapsedtimer.h
install -m 755  base/include/multimedia/media_attr_str.h -D $MDK_PATH/include/multimedia/media_attr_str.h
install -m 755  base/include/multimedia/media_buffer.h -D $MDK_PATH/include/multimedia/media_buffer.h
install -m 755  base/include/multimedia/media_meta.h -D $MDK_PATH/include/multimedia/media_meta.h
install -m 755  base/include/multimedia/media_monitor.h -D $MDK_PATH/include/multimedia/media_monitor.h
install -m 755  base/include/multimedia/mm_cpp_utils.h -D $MDK_PATH/include/multimedia/mm_cpp_utils.h
install -m 755  base/include/multimedia/mm_debug.h -D $MDK_PATH/include/multimedia/mm_debug.h
install -m 755  base/include/multimedia/mm_errors.h -D $MDK_PATH/include/multimedia/mm_errors.h
install -m 755  base/include/multimedia/mm_refbase.h -D $MDK_PATH/include/multimedia/mm_refbase.h
install -m 755  base/include/multimedia/mm_types.h -D $MDK_PATH/include/multimedia/mm_types.h
install -m 755  base/include/multimedia/mmlistener.h -D $MDK_PATH/include/multimedia/mmlistener.h
install -m 755  base/include/multimedia/mmmsgthread.h -D $MDK_PATH/include/multimedia/mmmsgthread.h
install -m 755  base/include/multimedia/mmparam.h -D $MDK_PATH/include/multimedia/mmparam.h
install -m 755  base/include/multimedia/mmthread.h -D $MDK_PATH/include/multimedia/mmthread.h
install -m 755  include/multimedia/mediaplayer.h -D $MDK_PATH/include/multimedia/mediaplayer.h
install -m 755  cow/include/multimedia/clock.h -D $MDK_PATH/include/multimedia/clock.h
install -m 755  cow/include/multimedia/component.h -D $MDK_PATH/include/multimedia/component.h
install -m 755  cow/include/multimedia/component_factory.h -D $MDK_PATH/include/multimedia/component_factory.h
install -m 755  cow/include/multimedia/pipeline.h  -D $MDK_PATH/include/multimedia/pipeline.h
install -m 755  cow/include/multimedia/pipeline_player_base.h -D $MDK_PATH/include/multimedia/pipeline_player_base.h

cd $MDK_PATH
rm -rf out
echo "clean mdk"
make clean
cd ..
echo "taring mdk"
tar -czvf ./mdk/yunos-mdk.tgz --exclude=mdk.tar.gz --exclude=update_mdk.sh --exclude=mm_readme.txt --exclude=yunos.mk mdk
cd -
echo "update mdk finish"
