# Request the minimum version of CMake, in case of lower version throws error
cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

# Define your project
project(
  "StatiCLib"
  VERSION 1.0.0
  DESCRIPTION "CMake C project template for operating systems II"
  LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 17)
# Flags de compilación
set(CMAKE_CXX_FLAGS "-Wall -pedantic -Wextra -Wconversion -std=gnu11 -fprofile-arcs -ftest-coverage")
# Include the cJSON library
include_directories(../../external/cJSON)

# Include the include directory, where the header files are located
include_directories(include)

# Add the src directory, where the source files are located
file(GLOB SOURCES "src/*.c" "src/*.cpp")

# Add cJSON source files
file(GLOB cJSON_SOURCES "../../external/cJSON/*.c")

# Create the executable
add_library(${PROJECT_NAME} STATIC ${SOURCES} ${cJSON_SOURCES})

# Link cJSON library to StatiCLib
target_link_libraries(${PROJECT_NAME} PUBLIC 
  rocksDBWrapper
  cJSON 
  nlohmann_json::nlohmann_json
  ZLIB::ZLIB
  )
