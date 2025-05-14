cmake_minimum_required(VERSION 3.5.1)

if(PROTOBUF_AS_SUBMODULE)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/protobuf)
  message(STATUS "Using protobuf via add_subdirectory.")

  set(Protobuf_LIBRARY libprotobuf)
  set(Protobuf_PROTOC_LIBRARY $<TARGET_FILE:protoc>)
elseif(PROTOBUF_FETCHCONTENT)
  message(STATUS "Using protobuf via add_subdirectory (FetchContent).")
  include(FetchContent)
  FetchContent_Declare(
    protobuf
    GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
    GIT_TAG        v30.2)
  FetchContent_MakeAvailable(protobuf)

  set(Protobuf_LIBRARY libprotobuf)
  set(Protobuf_PROTOC_LIBRARY $<TARGET_FILE:protoc>)
else()
  # Find Protobuf installation
  # Looks for protobuf-config.cmake file installed by Protobuf's cmake installation.
  set(protobuf_MODULE_COMPATIBLE TRUE)
  find_package(Protobuf REQUIRED)
  message(STATUS "Using protobuf ${Protobuf_VERSION}")
endif()
