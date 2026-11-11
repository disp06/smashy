cmake_minimum_required(VERSION 3.10)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(SmashyRoadVita C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Smashy Road: Wanted")
set(VITA_TITLEID  "SMSH00001")
set(VITA_VERSION  "01.00")
set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d ATTRIBUTE2=12")  # extended memory mode

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -ffast-math -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -Wall -Wno-unused-parameter -Wno-misleading-indentation -std=gnu11")

add_executable(${PROJECT_NAME}
  src/game.c src/render.c src/world.c src/assets.c src/audio.c src/gfx_gxm.c src/plat_vita.c)

target_link_libraries(${PROJECT_NAME}
  SceGxm_stub SceDisplay_stub SceCtrl_stub SceTouch_stub SceAudio_stub ScePower_stub
  SceAppUtil_stub SceCommonDialog_stub SceRtc_stub SceSysmodule_stub SceLibKernel_stub m)

vita_create_self(eboot.bin ${PROJECT_NAME} UNSAFE)
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE sce_sys/icon0.png sce_sys/icon0.png
  FILE sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
)
