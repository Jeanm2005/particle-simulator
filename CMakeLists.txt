cmake_minimum_required(VERSION 3.16)
project(QuantumAtomSimulator VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -O2)
endif()

set(CORE_SOURCES
    src/Element.cpp
    src/QuantumMath.cpp
    src/Orbital.cpp
    src/RadialSampler.cpp
    src/Simulation.cpp
    src/main.cpp
)

find_package(OpenGL QUIET)
find_package(GLEW QUIET)
find_package(glfw3 QUIET)
find_package(glm QUIET)

set(HAS_GL FALSE)
if(OpenGL_FOUND AND GLEW_FOUND AND glfw3_FOUND)
    set(HAS_GL TRUE)
endif()

if(NOT glm_FOUND)
    find_path(GLM_INCLUDE_DIR glm/glm.hpp
        PATHS /usr/include /usr/local/include)
    if(GLM_INCLUDE_DIR)
        set(glm_FOUND TRUE)
    endif()
endif()

if(HAS_GL AND glm_FOUND)
    message(STATUS "OpenGL visualiser: ENABLED")
    list(APPEND CORE_SOURCES src/Engine.cpp)
    add_executable(atom_sim ${CORE_SOURCES})
    target_compile_definitions(atom_sim PRIVATE QM_HAS_OPENGL=1)
    target_include_directories(atom_sim PRIVATE include ${GLM_INCLUDE_DIR})
    target_link_libraries(atom_sim PRIVATE OpenGL::GL GLEW::GLEW glfw)
    if(TARGET glm::glm)
        target_link_libraries(atom_sim PRIVATE glm::glm)
    endif()
else()
    message(STATUS "OpenGL visualiser: DISABLED (missing glfw/glew/glm/OpenGL)")
    message(STATUS "  Install: sudo apt install libglfw3-dev libglew-dev libglm-dev libgl1-mesa-dev")
    add_executable(atom_sim ${CORE_SOURCES})
    target_include_directories(atom_sim PRIVATE include)
endif()

install(TARGETS atom_sim DESTINATION bin)
install(DIRECTORY data/ DESTINATION share/atom_sim/data)