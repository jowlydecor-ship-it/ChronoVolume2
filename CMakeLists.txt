cmake_minimum_required(VERSION 3.15)
project(ChronoVolume LANGUAGES CXX)
if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# OpenFX SDK: use -DOFX_SDK=/path/to/openfx, otherwise it is downloaded automatically.
set(OFX_SDK "" CACHE PATH "Path to an OpenFX SDK checkout (optional)")
set(OFX_GIT_TAG "main" CACHE STRING "OpenFX git tag/branch used when downloading the SDK")
if(NOT OFX_SDK)
  include(FetchContent)
  FetchContent_Declare(openfx
    GIT_REPOSITORY https://github.com/AcademySoftwareFoundation/openfx.git
    GIT_TAG ${OFX_GIT_TAG} GIT_SHALLOW TRUE)
  FetchContent_GetProperties(openfx)
  if(NOT openfx_POPULATED)
    FetchContent_Populate(openfx)
  endif()
  set(OFX_SDK ${openfx_SOURCE_DIR})
endif()

set(SUPPORT ${OFX_SDK}/Support)
set(SUPPORT_SRC
  ${SUPPORT}/Library/ofxsCore.cpp
  ${SUPPORT}/Library/ofxsImageEffect.cpp
  ${SUPPORT}/Library/ofxsInteract.cpp
  ${SUPPORT}/Library/ofxsLog.cpp
  ${SUPPORT}/Library/ofxsMultiThread.cpp
  ${SUPPORT}/Library/ofxsParams.cpp
  ${SUPPORT}/Library/ofxsProperty.cpp
  ${SUPPORT}/Library/ofxsPropertyValidation.cpp)

find_package(Threads REQUIRED)

add_library(ChronoVolume MODULE ChronoVolume.cpp chrono_core.h ${SUPPORT_SRC})
target_include_directories(ChronoVolume PRIVATE ${OFX_SDK}/include ${SUPPORT}/include)
target_link_libraries(ChronoVolume PRIVATE Threads::Threads)
set_target_properties(ChronoVolume PROPERTIES PREFIX "" SUFFIX ".ofx")

# Assemble the .ofx.bundle next to the build output
set(BUNDLE ${CMAKE_BINARY_DIR}/ChronoVolume.ofx.bundle)
if(WIN32)
  set(ARCH_DIR Win64)
elseif(APPLE)
  set(ARCH_DIR MacOS)
else()
  set(ARCH_DIR Linux-x86-64)
endif()
add_custom_command(TARGET ChronoVolume POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${BUNDLE}/Contents/${ARCH_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:ChronoVolume> ${BUNDLE}/Contents/${ARCH_DIR}/ChronoVolume.ofx)
if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
  add_custom_command(TARGET ChronoVolume POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/Info.plist ${BUNDLE}/Contents/Info.plist
    COMMAND codesign --force --deep -s - ${BUNDLE})
endif()
