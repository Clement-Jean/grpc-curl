cmake_minimum_required(VERSION 3.20)
project(curl-grpc CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "-g -O0")

set(protobuf_INSTALL OFF)
set(protobuf_BUILD_TESTS OFF)
set(protobuf_BUILD_CONFORMANCE OFF)
set(protobuf_BUILD_EXAMPLES OFF)
set(protobuf_BUILD_PROTOBUF_BINARIES ON)
set(protobuf_BUILD_PROTOC_BINARIES ON)
set(protobuf_BUILD_LIBPROTOC ON)
set(protobuf_BUILD_LIBUPB "Build libupb" ON)

include(cmake/proto.cmake)
find_package(Protobuf REQUIRED)

include_directories(${Protobuf_INCLUDE_DIRS})

add_subdirectory(proto)

add_executable(grpc-curl main.cc)
target_link_libraries(grpc-curl
  PRIVATE
  protos
  curl
)
target_include_directories(grpc-curl
  PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
)
