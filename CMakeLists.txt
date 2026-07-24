cmake_minimum_required(VERSION 3.16)
project(MPMPESCoreCpp VERSION 0.4.22 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MPMPES_BUILD_TESTS "Build unit tests" OFF)
option(MPMPES_BUILD_EXAMPLE_NATIVE "Build example C native plugin .so" ON)

add_executable(mpmpes
  src/main.cpp
  src/binary/BinaryStream.cpp
  src/config/ServerProperties.cpp
  src/plugin/PluginManager.cpp
  src/raklib/UDPServerSocket.cpp
  src/raklib/RakLibConstants.cpp
  src/raklib/EncapsulatedPacket.cpp
  src/raklib/AcknowledgePacket.cpp
  src/raklib/Datagram.cpp
  src/raklib/Session.cpp
  src/raklib/SessionManager.cpp
  src/level/Chunk.cpp
  src/level/Level.cpp
  src/player/Player.cpp
  src/server/Server.cpp
  src/util/Logger.cpp
)

target_include_directories(mpmpes PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
target_link_libraries(mpmpes PRIVATE Threads::Threads ZLIB::ZLIB ${CMAKE_DL_LIBS})

if(MSVC)
  target_compile_options(mpmpes PRIVATE /W4)
else()
  target_compile_options(mpmpes PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
  target_link_libraries(mpmpes PRIVATE ws2_32)
endif()

if(MPMPES_BUILD_EXAMPLE_NATIVE)
  add_library(example_hello_c SHARED plugins/HelloC/hello_c.c)
  target_include_directories(example_hello_c PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  set_target_properties(example_hello_c PROPERTIES
    OUTPUT_NAME "hello_c"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/plugins/HelloC
    PREFIX "lib"
  )
  add_custom_command(TARGET example_hello_c POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:example_hello_c>
            ${CMAKE_CURRENT_SOURCE_DIR}/plugins/HelloC/libhello_c.so
  )

  add_library(example_hello_cpp SHARED plugins/HelloCpp/hello_cpp.cpp)
  target_include_directories(example_hello_cpp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  set_target_properties(example_hello_cpp PROPERTIES
    OUTPUT_NAME "hello_cpp"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/plugins/HelloCpp
    PREFIX "lib"
  )
  add_custom_command(TARGET example_hello_cpp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:example_hello_cpp>
            ${CMAKE_CURRENT_SOURCE_DIR}/plugins/HelloCpp/libhello_cpp.so
  )
endif()

install(TARGETS mpmpes RUNTIME DESTINATION bin)
