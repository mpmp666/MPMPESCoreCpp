cmake_minimum_required(VERSION 3.16)
project(MPMPESCoreCpp VERSION 0.5.0 LANGUAGES C CXX)

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
  src/level/LevelStorage.cpp
  src/level/Region.cpp
  src/nbt/Tag.cpp
  src/protocol/MultiProtocol.cpp
  src/item/Recipes.cpp
  src/entity/EntityNbt.cpp
  src/entity/Effects.cpp
  src/item/Enchantments.cpp
  src/item/Anvil.cpp
  src/item/Brewing.cpp
  src/network/QueryRcon.cpp
  src/level/gen/NormalGenerator.cpp
  src/level/gen/NormalChunk.cpp
  src/level/gen/NetherAstralChunk.cpp
  src/level/gen/EndGenerator.cpp
  src/level/gen/EndFeatures.cpp
  src/util/Sha256.cpp
  src/util/Json.cpp
  src/level/structure/StructurePlacer.cpp
  src/level/structure/EndCityLayout.cpp
  src/level/structure/Loot.cpp
  src/level/structure/Jigsaw.cpp
  src/level/structure/VillagePlacer.cpp
  src/level/structure/StructureService.cpp
  src/level/structure/Repository.cpp
  src/level/RandomTick.cpp
  src/level/FluidTick.cpp
  src/player/Player.cpp
  src/server/Server.cpp
  src/server/MapManager.cpp
  src/frp/FrpClient.cpp
  src/util/Logger.cpp
)

target_include_directories(mpmpes PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
target_link_libraries(mpmpes PRIVATE Threads::Threads ZLIB::ZLIB ${CMAKE_DL_LIBS} ssl crypto)

if(MSVC)
  target_compile_options(mpmpes PRIVATE /W4)
else()
  target_compile_options(mpmpes PRIVATE -Wall -Wextra -Wpedantic)
  # RISC-V needs libatomic for sub-word atomics. Link AFTER objects via
  # target_link_libraries (CMAKE_EXE_LINKER_FLAGS puts -latomic too early).
  if(UNIX AND NOT APPLE)
    target_link_libraries(mpmpes PRIVATE atomic)
  endif()
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

if(MPMPES_BUILD_TESTS)
  enable_testing()
  add_executable(mp_translation_diff tests/mp_translation_diff.cpp src/protocol/MultiProtocol.cpp src/nbt/Tag.cpp)
  target_include_directories(mp_translation_diff PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(mp_translation_diff PRIVATE ZLIB::ZLIB)
  add_test(NAME mp_translation_diff
    COMMAND mp_translation_diff ${CMAKE_CURRENT_SOURCE_DIR}/tests/ref_php.txt)
  add_executable(mp_incoming_test tests/mp_incoming_test.cpp src/protocol/MultiProtocol.cpp src/nbt/Tag.cpp src/binary/BinaryStream.cpp)
  target_include_directories(mp_incoming_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(mp_incoming_test PRIVATE ZLIB::ZLIB)
  add_test(NAME mp_incoming_test COMMAND mp_incoming_test)
  add_executable(mp_crafting_diff tests/mp_crafting_diff.cpp src/item/Recipes.cpp src/protocol/MultiProtocol.cpp src/nbt/Tag.cpp src/binary/BinaryStream.cpp)
  target_include_directories(mp_crafting_diff PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(mp_crafting_diff PRIVATE ZLIB::ZLIB)
  add_test(NAME mp_crafting_diff COMMAND mp_crafting_diff WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()
