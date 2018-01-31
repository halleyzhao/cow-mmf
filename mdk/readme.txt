#### Version 0.1
Release date 2016-11-10

#### Hardware Requirement
- PC with Intel Gen8+ gfx is recommended
- general PC is still ok

#### Software Requirement
- Ubuntu 16.10 is recommend
- other Linux distribution should upgrade ffmpeg to recent version

#### Installation
1. tar xvf yunos-mdk.tar.gz
2. cd mdk
3. make && make install
4. Library files and executable files will build into mdk/out/ and install into the fakeroot/

#### Run
1. example test:
 ./run-test
 it will use examplepipeline-test to playback a  h264 bytestream
2. cowplayer-test
 change the command to cowplayer-test at the end of run-test
 ./run-test

#### hw acceleration
with Intel Gen8+ gfx, you can enale hw acceleration
1. install Intel hw codec, refer to: https://github.com/01org/libyami
2. change VideoDecodeV4l2 and VideoSinkEGL priority to 'high' in ow_plugins_ubt.xml
