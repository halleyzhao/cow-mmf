# cow-mmf
it is cow multimedia framework of alios, removed something not important.

## cow multiemdia framework
it is a media framework inspired by GStreamer design philosophy: modular, extensiable, intuitive API; modules exchange buffer automatically (w/o help from client).
we use C++ to implement the framework, it makes code tidy and easy to maintain.
we drop some flexibility that GStreamer tries to achieve (which isn't interested by major users).
in short, we write 10% code of GStreamer (core framework), achieve 90% flexbility that GStreamer does. It is my solution for multimedia.

## V4L2Codec
There is no standard for Video Codec interface (between silicon vendor and operating system). (like OpenGL for 3D, V4L2 for camera and ALSA for audio).
V4L2 interface is widely used with Linux, it can be borrowed to define video codec interface.
I would rather treat OpenMAX-IL as media framework than codec component. OMX-IL components is hard to use because of its framework attributes and async API, Android has to implement MediaCodec to wrap it. OpenMAX-IL+MediaCodec is more complex than V4L2Codec.

## current status
the project is developed on Linux/Ubuntu at begining, and can be built/run on Android; but lack of maintainance on either Ubuntu or Android recently.
maybe, in the future; we can use it on Linux and other platform, for its simple and efficiency.
