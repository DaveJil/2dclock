cmake_minimum_required(VERSION 3.16)
project(Clock2D LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Silence macOS OpenGL deprecation warnings
add_compile_definitions(GL_SILENCE_DEPRECATION)

# Find GLFW (install via Homebrew: brew install glfw)
find_package(glfw3 3.3 REQUIRED)

add_executable(clock2d src/main.cpp)

# Link GLFW and required Apple frameworks
target_link_libraries(clock2d PRIVATE glfw
  "-framework OpenGL"
  "-framework Cocoa"
  "-framework IOKit"
  "-framework CoreVideo"
)

# Request a macOS Core profile at compile time (also set at runtime via GLFW hints)
if(APPLE)
  target_compile_definitions(clock2d PRIVATE MAC_OSX)
endif()
