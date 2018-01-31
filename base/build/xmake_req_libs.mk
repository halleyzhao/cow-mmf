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


ifeq ($(REQUIRE_PULSEAUDIO),1)
  ifeq ($(XMAKE_ENABLE_PULSEAUDIO_3),true)
    LOCAL_SHARED_LIBRARIES += libpulse libpulsecore-5.0 libpulse-simple libpulsecommon-5.0
    ifeq ($(REQUIRE_PULSEAUDIO_MODULE),1)
      LOCAL_SHARED_LIBRARIES += libprotocol-native
    endif
  else
    LOCAL_REQUIRED_MODULES += pulseaudio
    LOCAL_LDFLAGS += -lpulse -lpulse-simple -lpulsecore-5.0 -lpulsecommon-5.0
    ifeq ($(REQUIRE_PULSEAUDIO_MODULE),1)
      LOCAL_LDFLAGS += libprotocol-native
    endif
  endif
  LOCAL_LDFLAGS += -L$(XMAKE_BUILD_OUT)/target/rootfs/usr/lib/pulse-5.0/modules
endif

ifeq ($(REQUIRE_PROPERTIES),1)
  ifeq ($(XMAKE_ENABLE_PROPERTIES_3),true)
   LOCAL_SHARED_LIBRARIES += libproperties
  else
   LOCAL_REQUIRED_MODULES += properties
   LOCAL_LDFLAGS += -lproperties
  endif
endif

ifeq ($(REQUIRE_LIBHYBRIS),1)
  ifeq ($(XMAKE_ENABLE_LIBHYBRIS_3),true)
   LOCAL_SHARED_LIBRARIES += libhybris-common
  else
   LOCAL_REQUIRED_MODULES += libhybris
   LOCAL_LDFLAGS += -lhybris-common
  endif
endif

ifeq ($(REQUIRE_LIBUV),1)
  ifeq ($(XMAKE_ENABLE_LIBUV_3),true)
   LOCAL_SHARED_LIBRARIES += libuv
  else
   LOCAL_REQUIRED_MODULES += libuv
   LOCAL_LDFLAGS += -luv
  endif
endif

ifeq ($(REQUIRE_LIBAV),1)
  ifeq ($(XMAKE_ENABLE_LIBAV_3),true)
    ifeq ($(USING_FORCE_FFMPEG),1)
      LOCAL_SHARED_LIBRARIES += lib_ff_avformat lib_ff_avcodec lib_ff_avutil lib_ff_swresample lib_ff_swscale
    else
      LOCAL_SHARED_LIBRARIES += libavformat libavcodec libavutil libswresample libswscale
    endif
  else
   LOCAL_REQUIRED_MODULES += libav
   LOCAL_LDFLAGS += -lavformat -lavcodec -lavutil -lswresample -lswscale
  endif
endif

ifeq ($(REQUIRE_LIBJPEG_TURBO),1)
  ifeq ($(XMAKE_ENABLE_LIBJPEG_TURBO_3),true)
   LOCAL_SHARED_LIBRARIES += libjpeg libturbojpeg
  else
   LOCAL_REQUIRED_MODULES += libjpeg-turbo
   LOCAL_LDFLAGS += -ljpeg -lturbojpeg
  endif
endif

ifeq ($(REQUIRE_EXPAT),1)
  ifeq ($(XMAKE_ENABLE_EXPAT_3),true)
   LOCAL_SHARED_LIBRARIES += libexpat
  else
   LOCAL_REQUIRED_MODULES += expat
   LOCAL_LDFLAGS +=  -lexpat
  endif
endif

ifeq ($(REQUIRE_AUDIOSERVER),1)
  ifeq ($(XMAKE_ENABLE_AUDIOSERVER_3),true)
   LOCAL_SHARED_LIBRARIES += libaudio
  else
   LOCAL_REQUIRED_MODULES += audioserver
   LOCAL_LDFLAGS += -laudio
  endif
endif
ifeq ($(REQUIRE_EXIF),1)
  ifeq ($(XMAKE_ENABLE_EXIF_3),true)
   LOCAL_SHARED_LIBRARIES += libexif
  else
   LOCAL_REQUIRED_MODULES += libexif
   LOCAL_LDFLAGS +=  -lexif
  endif
endif

ifeq ($(REQUIRE_PNG),1)
  ifeq ($(XMAKE_ENABLE_LIBPNG_3),true)
   LOCAL_STATIC_LIBRARIES += libpng
  else
   LOCAL_REQUIRED_MODULES += libpng
   LOCAL_LDFLAGS +=  -lpng
  endif
endif

ifeq ($(REQUIRE_GIF),1)
  ifeq ($(XMAKE_ENABLE_GIF_3),true)
   LOCAL_SHARED_LIBRARIES += libgif
  else
   LOCAL_REQUIRED_MODULES += giflib
   LOCAL_LDFLAGS +=  -lgif
  endif
endif

ifeq ($(REQUIRE_GTEST),1)
  ifeq ($(XMAKE_ENABLE_GTEST_3),true)
   LOCAL_SHARED_LIBRARIES += libgtest libgtest_main
  else
   LOCAL_REQUIRED_MODULES += gtest
   LOCAL_LDFLAGS +=  -lgtest -lgtest_main
  endif
endif


ifeq ($(REQUIRE_PAGEWINDOW),1)
  ifeq ($(XMAKE_ENABLE_PAGEWINDOW_3),true)
   LOCAL_SHARED_LIBRARIES += libpagewindow
  else
   LOCAL_REQUIRED_MODULES += pagewindow
   LOCAL_LDFLAGS += -lpagewindow
  endif
endif


ifeq ($(REQUIRE_POWER),1)
  ifeq ($(XMAKE_ENABLE_POWER_3),true)
   LOCAL_SHARED_LIBRARIES += libpower_client
  else
   LOCAL_REQUIRED_MODULES += power
   LOCAL_LDFLAGS += -lpower_client
  endif
endif

ifeq ($(REQUIRE_LIBWEBP),1)
  ifeq ($(XMAKE_ENABLE_LIBWEBP_3),true)
   LOCAL_SHARED_LIBRARIES += libwebp
  else
   LOCAL_REQUIRED_MODULES += libwebp
   LOCAL_LDFLAGS += -lwebp
  endif
endif

ifeq ($(REQUIRE_ZLIB),1)
  ifeq ($(XMAKE_ENABLE_ZLIB_3),true)
   LOCAL_SHARED_LIBRARIES += libz
  else
   LOCAL_REQUIRED_MODULES += zlib
   LOCAL_LDFLAGS += -lz
  endif
endif

ifeq ($(REQUIRE_WAYLAND),1)
  ifeq ($(XMAKE_ENABLE_WAYLAND_3),true)
   LOCAL_SHARED_LIBRARIES += libwayland-cursor libwayland-server libwayland-client
  else
   LOCAL_REQUIRED_MODULES += wayland
   LOCAL_LDFLAGS += -lwayland-cursor -lwayland-server -lwayland-client
  endif
endif

ifeq ($(REQUIRE_EGL),1)
  ifeq ($(XMAKE_ENABLE_LIBHYBRIS_3),true)
   LOCAL_SHARED_LIBRARIES += libEGL libGLESv2
  else
   LOCAL_REQUIRED_MODULES += libhybris
   LOCAL_LDFLAGS +=  -lEGL -lGLESv2
  endif
endif

ifeq ($(REQUIRE_SURFACE),1)
  ifeq ($(XMAKE_ENABLE_SURFACE_3),true)
   LOCAL_SHARED_LIBRARIES += libgui libysurface libgfx-cutils
   ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
    ifeq ($(XMAKE_ENABLE_UNIFIED_SURFACE),true)
       LOCAL_SHARED_LIBRARIES += surface_wrapper
    endif
   endif
  else
     ifeq ($(XMAKE_ENABLE_CNTR_HAL),true)
       ifeq ($(XMAKE_ENABLE_UNIFIED_SURFACE),true)
          LOCAL_REQUIRED_MODULES += surface_wrapper
          LOCAL_LDFLAGS +=  -lsurface_wrapper
       endif
     endif
   LOCAL_REQUIRED_MODULES += surface
   LOCAL_LDFLAGS +=  -lgui
  endif
endif

ifeq ($(REQUIRE_WPC),1)
  ifeq ($(XMAKE_ENABLE_WPC_3),true)
   LOCAL_SHARED_LIBRARIES += libwpc
  else
   LOCAL_REQUIRED_MODULES += wpc
   LOCAL_LDFLAGS +=  -lwpc
  endif
endif

ifeq ($(REQUIRE_COMPAT),1)
  ifeq ($(XMAKE_ENABLE_CONTAINERD_3),true)
   LOCAL_SHARED_LIBRARIES += libmedia_compat_layer
  else
   LOCAL_REQUIRED_MODULES += containerd
   LOCAL_LDFLAGS +=  -lmedia_compat_layer
  endif
   LOCAL_LDFLAGS += -L$(XMAKE_BUILD_OUT)/target/rootfs/usr/lib/compat
endif

ifeq ($(REQUIRE_CAMERASVR),1)
  ifeq ($(XMAKE_ENABLE_CAMERASERVER_3),true)
    ifeq ($(YUNOS_SYSCAP_MM.CAMERA_VERSION_30),true)
      LOCAL_SHARED_LIBRARIES += libcamera_client libcamera_common
    else
      LOCAL_SHARED_LIBRARIES += libcamerav2 libcamerautil
    endif
  else
   LOCAL_REQUIRED_MODULES += cameraserver
   ifeq ($(YUNOS_SYSCAP_MM.CAMERA_VERSION_30),true)
      LOCAL_LDFLAGS += -lcamera_client -lcamera_common
   else
      LOCAL_LDFLAGS += -lcamerav2 -lcamerautil
   endif
  endif
endif

ifeq ($(REQUIRE_PERMISSION),1)
  ifeq ($(XMAKE_ENABLE_PERMISSION_3),true)
    LOCAL_SHARED_LIBRARIES += libpermission
  else
    LOCAL_REQUIRED_MODULES += permission
    LOCAL_LDFLAGS += -lpermission
  endif
endif

ifeq ($(YUNOS_SYSCAP_RESMANAGER),true)
  LOCAL_SHARED_LIBRARIES += libdata
endif

ifeq ($(REQUIRE_RESOURCELOCATOR),1)
    ifeq ($(XMAKE_ENABLE_RESOURCELOCATOR_3),true)
        LOCAL_SHARED_LIBRARIES += libresourcelocator
    else
        LOCAL_REQUIRED_MODULES += resourcelocator
        LOCAL_LDFLAGS += -lresourcelocator
    endif
endif

ifeq ($(REQUIRE_LIBDCE),1)
    ifeq ($(XMAKE_ENABLE_LIBDCE_3),true)
        LOCAL_SHARED_LIBRARIES += libdce
    else
        LOCAL_REQUIRED_MODULES += libdce
        LOCAL_LDFLAGS += -ldce
    endif
endif
ifeq ($(REQUIRE_LIBDRM),1)
    ifeq ($(XMAKE_ENABLE_LIBDRM_3),true)
        LOCAL_SHARED_LIBRARIES += libdrm libdrm_omap
    else
        LOCAL_REQUIRED_MODULES += libdrm
        LOCAL_LDFLAGS += -ldrm -ldrm_omap
    endif
endif
ifeq ($(REQUIRE_LIBMMRPC),1)
    ifeq ($(XMAKE_ENABLE_TI_IPC_3),true)
        LOCAL_SHARED_LIBRARIES += libmmrpc
    else
        LOCAL_REQUIRED_MODULES += ti-ipc
        LOCAL_LDFLAGS += -lmmrpc
    endif
endif