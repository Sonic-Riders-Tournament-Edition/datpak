cmake_minimum_required(VERSION 3.0)
project(DatPak)

include_directories(include)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_GLIBCXX_DEBUG")
set(CMAKE_CXX_STANDARD 20)

add_executable(DatPak main.cpp gcaxArchive.cpp helpers/membuf.cpp helpers/memstream.cpp)
target_link_libraries(DatPak PUBLIC DspTool)