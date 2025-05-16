set(PROTOC_EXECUTABLE ${protobuf_BINARY_DIR}/src/protoc CACHE FILEPATH "Path to the protoc executable")

add_library(protos reflection.proto)
protobuf_generate(
  TARGET protos
  LANGUAGE cpp
  PROTOC_EXE ${PROTOC_EXECUTABLE}
)

target_link_libraries(protos
  ${Protobuf_LIBRARIES}
)
