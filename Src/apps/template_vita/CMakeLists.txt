cmake_minimum_required(VERSION 3.16)

# ----------------------------------------------------------------------------
# AppGameKit — PS Vita template build
#
# Builds the engine ENGINE_SRC + the Vita platform layer + this template into
# a single .vpk. Run with:
#
#   set VITASDK=C:/vitasdk          (Windows)
#   export VITASDK=/usr/local/vitasdk (Unix)
#
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=%VITASDK%/share/vita.toolchain.cmake
#   cmake --build build
#
# The result is build/agk_template_vita.vpk.
# ----------------------------------------------------------------------------

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path.")
  endif()
endif()

project(agk_template_vita CXX C)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "AGK Vita Template")
set(VITA_TITLEID  "AGKT00001")
set(VITA_VERSION  "01.00")

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_C_STANDARD   99)

# AGK_VITA selects the right code paths inside the engine. _AGK_LIBRARY hides
# AGK's internal global symbols where appropriate (matches the Pi build flag).
add_compile_definitions(
  AGK_VITA
  IDE_VITA
  _AGK_ERROR_CHECK
  HAVE_M_PI
  DISABLE_VULKAN   # Vita has no Vulkan; skip the renderer/Vulkan/* include block
  # zlib's ioapi.c uses LFS variants by default (fopen64 etc.) which newlib
  # on the Vita doesn't ship. Redirect to plain stdio at compile time.
  fopen64=fopen
  ftello64=ftell
  fseeko64=fseek
  # giflib uses Windows-style typedefs (UINT32) that aren't declared in any
  # of its own headers; supply them via -D.
  UINT32=uint32_t
  # giflib gates includes/code behind HAVE_*_H feature flags.
  HAVE_FCNTL_H=1
  HAVE_UNISTD_H=1
  HAVE_STDARG_H=1
)

set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -Wl,-q -Wall -O2 -ffast-math -fsigned-char -include stdint.h -Wno-incompatible-pointer-types -Wno-int-conversion -Wno-implicit-function-declaration -Wno-error")
# assimp's Exporter.cpp uses dynamic_cast, so RTTI must stay enabled.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,-q -Wall -O2 -ffast-math -fsigned-char -fexceptions -frtti -fpermissive -include stdint.h -Wno-error")

# ----------------------------------------------------------------------------
# Source roots
# ----------------------------------------------------------------------------
set(AGK_ROOT   "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(COMMON_SRC "${AGK_ROOT}/common")
set(VITA_SRC   "${AGK_ROOT}/platform/vita/Source")
set(REND_SRC   "${AGK_ROOT}/renderer/OpenGLES2")

include_directories(
  ${COMMON_SRC}/include
  ${COMMON_SRC}
  ${AGK_ROOT}/renderer
  ${AGK_ROOT}/giflib
  ${AGK_ROOT}/zxing
  ${AGK_ROOT}/libpng
  ${AGK_ROOT}/libjpeg
  ${AGK_ROOT}/zlib
  ${AGK_ROOT}/assimp
  ${AGK_ROOT}/bullet
  ${AGK_ROOT}/bullet/BulletCollision/CollisionShapes
  ${AGK_ROOT}/libogg/include
  ${AGK_ROOT}/freetype/include
  ${REND_SRC}
  ${VITA_SRC}
  ${CMAKE_CURRENT_SOURCE_DIR}
)

# ----------------------------------------------------------------------------
# Engine sources (matches PiMakefile's _OBJS list, restricted to platform-
# agnostic compilation units).
# ----------------------------------------------------------------------------
set(ENGINE_SRC
  ${COMMON_SRC}/Source/3DMath.cpp
  ${COMMON_SRC}/Source/3DParticles.cpp
  ${COMMON_SRC}/Source/AGKFont.cpp
  ${COMMON_SRC}/Source/AGKLights.cpp
  ${COMMON_SRC}/Source/AGKMotionState.cpp
  ${COMMON_SRC}/Source/AGKShader.cpp
  ${COMMON_SRC}/Source/AGKSkyBox.cpp
  ${COMMON_SRC}/Source/AGKToBullet.cpp
  ${COMMON_SRC}/Source/AGO.cpp
  ${COMMON_SRC}/Source/AppErrors.cpp
  ${COMMON_SRC}/Source/BaseItem.cpp
  ${COMMON_SRC}/Source/CharacterController.cpp
  ${COMMON_SRC}/Source/CollisionShapes.cpp
  ${COMMON_SRC}/Source/ContactReport.cpp
  ${COMMON_SRC}/Source/DebugDraw.cpp
  ${COMMON_SRC}/Source/DynamicsWorld.cpp
  ${COMMON_SRC}/Source/FrameBuffer.cpp
  ${COMMON_SRC}/Source/GLDebugDrawer.cpp
  ${COMMON_SRC}/Source/JSONElement.cpp
  ${COMMON_SRC}/Source/Joint.cpp
  ${COMMON_SRC}/Source/Joints.cpp
  ${COMMON_SRC}/Source/Joystick.cpp
  ${COMMON_SRC}/Source/Mouse.cpp
  ${COMMON_SRC}/Source/Network.cpp
  ${COMMON_SRC}/Source/Particles.cpp
  ${COMMON_SRC}/Source/Physics.cpp
  ${COMMON_SRC}/Source/Ragdoll.cpp
  ${COMMON_SRC}/Source/RagdollBone.cpp
  ${COMMON_SRC}/Source/Ray.cpp
  ${COMMON_SRC}/Source/RigidBodies.cpp
  ${COMMON_SRC}/Source/RigidBody.cpp
  ${COMMON_SRC}/Source/SimplexNoise.cpp
  ${COMMON_SRC}/Source/Skeleton2D.cpp
  ${COMMON_SRC}/Source/Skeleton3D.cpp
  ${COMMON_SRC}/Source/StaticPlane.cpp
  ${COMMON_SRC}/Source/Tween.cpp
  ${COMMON_SRC}/Source/Vector.cpp
  ${COMMON_SRC}/Source/Wrapper.cpp
  ${COMMON_SRC}/Source/ZipFile.cpp
  ${COMMON_SRC}/Source/cCamera.cpp
  ${COMMON_SRC}/Source/cEvent.cpp
  ${COMMON_SRC}/Source/cFile.cpp
  ${COMMON_SRC}/Source/cForce.cpp
  ${COMMON_SRC}/Source/cHashedList.cpp
  ${COMMON_SRC}/Source/cImage.cpp
  ${COMMON_SRC}/Source/cMesh.cpp
  ${COMMON_SRC}/Source/cNode.cpp
  ${COMMON_SRC}/Source/cObject3D.cpp
  ${COMMON_SRC}/Source/cObjectMgr.cpp
  ${COMMON_SRC}/Source/cSound.cpp
  ${COMMON_SRC}/Source/cSprite.cpp
  ${COMMON_SRC}/Source/cSpriteMgr.cpp
  ${COMMON_SRC}/Source/cText.cpp
  ${COMMON_SRC}/Source/cTextEntry.cpp
  ${COMMON_SRC}/Source/cTouch.cpp
  ${COMMON_SRC}/Source/sha1.cpp
  ${COMMON_SRC}/Source/sha2.cpp
  ${COMMON_SRC}/Source/uString.cpp
)

# Box2D — same files the Pi build picks up.
file(GLOB BOX2D_SRC
  ${COMMON_SRC}/Box2D/Collision/*.cpp
  ${COMMON_SRC}/Box2D/Collision/Shapes/*.cpp
  ${COMMON_SRC}/Box2D/Common/*.cpp
  ${COMMON_SRC}/Box2D/Dynamics/*.cpp
  ${COMMON_SRC}/Box2D/Dynamics/Contacts/*.cpp
  ${COMMON_SRC}/Box2D/Dynamics/Joints/*.cpp
  ${COMMON_SRC}/Box2D/Rope/*.cpp
)

file(GLOB COLLISION_SRC ${COMMON_SRC}/Collision/*.cpp)

# Full Bullet physics tree (matches what the engine's physics commands need).
file(GLOB_RECURSE BULLET_SRC
  ${AGK_ROOT}/bullet/LinearMath/*.cpp
  ${AGK_ROOT}/bullet/BulletCollision/*.cpp
  ${AGK_ROOT}/bullet/BulletDynamics/*.cpp
  ${AGK_ROOT}/bullet/BulletFileLoader/*.cpp
  ${AGK_ROOT}/bullet/BulletWorldImporter/*.cpp
)

# zxing QR/barcode reader.
file(GLOB_RECURSE ZXING_SRC ${AGK_ROOT}/zxing/*.cpp)

# Assimp 3D model importer (+ its bundled C unicode helper).
file(GLOB_RECURSE ASSIMP_SRC ${AGK_ROOT}/assimp/*.cpp)
list(APPEND ASSIMP_SRC ${AGK_ROOT}/assimp/include/ConvertUTF/ConvertUTF.c)

# Renderer
set(RENDERER_SRC ${REND_SRC}/OpenGL_ES2.cpp)

# Vita platform
set(VITA_PLATFORM_SRC
  ${VITA_SRC}/VitaCore.cpp
  ${VITA_SRC}/VitaNetwork.cpp
  ${VITA_SRC}/VitaFileSystem.cpp
  ${VITA_SRC}/VitaAudio.cpp
  ${VITA_SRC}/VitaMisc.cpp
)

# Bundled third-party C deps (zlib/libpng/libjpeg/giflib) — pulled in only
# if those folders exist; otherwise we fall back to the vdpm-provided ports.
set(THIRD_PARTY_SRC "")
file(GLOB ZLIB_SRC ${AGK_ROOT}/zlib/*.c)
# iowin32.c is Windows-specific; minizip.c / miniunz.c are CLI tools.
list(FILTER ZLIB_SRC EXCLUDE REGEX ".*(iowin32|minizip|miniunz)\\.c$")
list(APPEND THIRD_PARTY_SRC ${ZLIB_SRC})
file(GLOB PNG_SRC  ${AGK_ROOT}/libpng/*.c)
list(APPEND THIRD_PARTY_SRC ${PNG_SRC})
file(GLOB JPEG_SRC ${AGK_ROOT}/libjpeg/*.c)
# Exclude the cjpeg/djpeg CLI tools and Windows-only utility files — the
# library proper is just the j*.c sources matching the Pi build.
list(FILTER JPEG_SRC EXCLUDE REGEX
  ".*(cdjpeg|rdjpgcom|wrjpgcom|cjpeg|djpeg|jpegtran|jmemnobs|jmemname|rdcolmap|rdbmp|rdgif|rdppm|rdrle|rdswitch|rdtarga|wrbmp|wrgif|wrppm|wrrle|wrtarga|example|jmemdos|jmemmac|ckconfig|jmemsys|ansi2knr)\\.c$")
list(APPEND THIRD_PARTY_SRC ${JPEG_SRC})
file(GLOB GIF_SRC  ${AGK_ROOT}/giflib/*.c)
list(APPEND THIRD_PARTY_SRC ${GIF_SRC})

# Game / template
set(TEMPLATE_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/Core.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/template.cpp
)

add_executable(${PROJECT_NAME}
  ${ENGINE_SRC}
  ${BOX2D_SRC}
  ${COLLISION_SRC}
  ${BULLET_SRC}
  ${ZXING_SRC}
  ${ASSIMP_SRC}
  ${RENDERER_SRC}
  ${VITA_PLATFORM_SRC}
  ${THIRD_PARTY_SRC}
  ${TEMPLATE_SRC}
)

# ----------------------------------------------------------------------------
# Link libraries: vitaGL stack + Sce stubs + standard libs
# ----------------------------------------------------------------------------
target_link_libraries(${PROJECT_NAME}
  vitaGL
  vitashark
  SceShaccCgExt
  taihen_stub
  SceShaccCg_stub
  mathneon
  SceLibKernel_stub
  SceCtrl_stub
  SceTouch_stub
  SceDisplay_stub
  SceGxm_stub
  SceCommonDialog_stub
  ScePower_stub
  SceSysmodule_stub
  SceAppMgr_stub
  SceAppUtil_stub
  SceAudio_stub
  SceIofilemgr_stub
  SceRtc_stub
  SceMotion_stub
  SceProcessmgr_stub
  SceNet_stub
  SceNetCtl_stub
  SceKernelDmacMgr_stub
  # FreeType (font rendering) + Vorbis (OGG decode) — prebuilt vdpm packages.
  # zlib/libpng come from THIRD_PARTY_SRC (compiled from AGK's bundled copy),
  # so they are NOT linked here to avoid duplicate symbols. bz2 is only a
  # FreeType dependency and AGK doesn't bundle it.
  freetype
  vorbisfile
  vorbis
  ogg
  bz2
  pthread
  m
  c
  stdc++
)
# vitaGL needs the C++ runtime — use the C++ linker driver.
set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE)
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME    ${VITA_APP_NAME}
  FILE sce_sys/icon0.png                          sce_sys/icon0.png
  FILE sce_sys/livearea/contents/bg.png           sce_sys/livearea/contents/bg.png
  FILE sce_sys/livearea/contents/startup.png      sce_sys/livearea/contents/startup.png
  FILE sce_sys/livearea/contents/template.xml     sce_sys/livearea/contents/template.xml
  FILE media/logo.png                             logo.png
  FILE media/beep.wav                             beep.wav
  FILE media/music.ogg                            music.ogg
)
