cmake_minimum_required(VERSION 3.16)
project(QuantumAtomSimulator VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/W4>)
else()
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wall> $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
        $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic> $<$<COMPILE_LANGUAGE:CXX>:-O2>)
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
    list(APPEND CORE_SOURCES src/Engine.cpp src/ShaderProgram.cpp)
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

include(GNUInstallDirs)
file(RELATIVE_PATH QM_INSTALL_DATA_FROM_BIN
    "${CMAKE_INSTALL_FULL_BINDIR}" "${CMAKE_INSTALL_FULL_DATADIR}/atom_sim/data")
configure_file(cmake/DataPaths.hpp.in generated/DataPaths.hpp @ONLY)
target_include_directories(atom_sim PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
install(TARGETS atom_sim DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY data/ DESTINATION ${CMAKE_INSTALL_DATADIR}/atom_sim/data)
include(CTest)
if(BUILD_TESTING)
    add_executable(screening_tests
        tests/ScreeningTests.cpp
        src/Element.cpp
        src/QuantumMath.cpp
        src/Orbital.cpp
        src/RadialSampler.cpp
        src/Simulation.cpp)
    target_include_directories(screening_tests PRIVATE include "${CMAKE_CURRENT_BINARY_DIR}/generated")
    add_test(NAME screening_regressions COMMAND screening_tests)
    set_tests_properties(screening_regressions PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

if(BUILD_TESTING)
    add_executable(physics_tests tests/PhysicsTests.cpp
        src/QuantumMath.cpp src/Orbital.cpp src/RadialSampler.cpp src/Simulation.cpp)
    target_include_directories(physics_tests PRIVATE include)
    add_test(NAME physics_regressions COMMAND physics_tests)
    add_test(NAME console_regressions COMMAND ${CMAKE_COMMAND}
        "-DATOM_SIM=$<TARGET_FILE:atom_sim>"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/ConsoleTests.cmake")
endif()

if(BUILD_TESTING)
    add_test(NAME installation_regressions COMMAND ${CMAKE_COMMAND}
        "-DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}"
        "-DBIN_DIR=${CMAKE_INSTALL_BINDIR}"
        "-DDATA_DIR=${CMAKE_INSTALL_DATADIR}"
        "-DCONFIG=$<CONFIG>"
        "-DBINARY_NAME=$<TARGET_FILE_NAME:atom_sim>"
        "-DAPI_BINARY_NAME=$<TARGET_FILE_NAME:atom_api>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/InstallationTests.cmake")
endif()

if(BUILD_TESTING AND HAS_GL AND glm_FOUND)
    find_path(EGL_INCLUDE_DIR EGL/egl.h)
    find_library(EGL_LIBRARY EGL)
    if(EGL_INCLUDE_DIR AND EGL_LIBRARY)
        add_executable(shader_tests tests/ShaderTests.cpp src/ShaderProgram.cpp
            src/QuantumMath.cpp src/RadialSampler.cpp)
        target_include_directories(shader_tests PRIVATE include ${EGL_INCLUDE_DIR})
        target_link_libraries(shader_tests PRIVATE OpenGL::GL GLEW::GLEW ${EGL_LIBRARY})
        add_test(NAME shader_regressions COMMAND shader_tests)
        set_tests_properties(shader_regressions PROPERTIES
            SKIP_RETURN_CODE 77
            ENVIRONMENT "EGL_PLATFORM=surfaceless;LIBGL_ALWAYS_SOFTWARE=1")
    endif()
endif()

option(QM_ENABLE_CUDA "Use CUDA sampling and probability-current updates when available" ON)
set(QM_COMPUTE_SOURCE src/ComputeBackend.cpp)
if(QM_ENABLE_CUDA)
    include(CheckLanguage)
    check_language(CUDA)
    if(CMAKE_CUDA_COMPILER)
        enable_language(CUDA)
        set(QM_COMPUTE_SOURCE cuda/ComputeBackend.cu)
        message(STATUS "CUDA compute: ENABLED (runtime CPU fallback)")
    else()
        message(STATUS "CUDA compute: DISABLED (no CUDA compiler; CPU fallback)")
    endif()
endif()
add_library(qm_compute STATIC ${QM_COMPUTE_SOURCE} src/ComputeDispatch.cpp)
target_include_directories(qm_compute PUBLIC include)
target_compile_options(qm_compute PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:-O2>)
set_target_properties(qm_compute PROPERTIES CUDA_STANDARD 17 CUDA_STANDARD_REQUIRED ON)
target_link_libraries(atom_sim PRIVATE qm_compute)
add_library(qm_model STATIC src/OrbitalModel.cpp src/Element.cpp src/QuantumMath.cpp
    src/Orbital.cpp src/RadialSampler.cpp src/Simulation.cpp)
target_include_directories(qm_model PUBLIC include PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(qm_model PUBLIC qm_compute)
add_executable(atom_api src/ApiWorker.cpp)
target_link_libraries(atom_api PRIVATE qm_model)
install(TARGETS atom_api DESTINATION ${CMAKE_INSTALL_BINDIR})
if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME transport_regressions COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/TransportTests.py" "$<TARGET_FILE:atom_api>")
        set_tests_properties(transport_regressions PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    add_executable(model_tests tests/ModelTests.cpp)
    target_link_libraries(model_tests PRIVATE qm_model)
    add_test(NAME model_regressions COMMAND model_tests)
    set_tests_properties(model_regressions PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    add_executable(dispatch_tests tests/DispatchTests.cpp src/ComputeDispatch.cpp
        src/QuantumMath.cpp src/RadialSampler.cpp)
    target_include_directories(dispatch_tests PRIVATE include)
    add_test(NAME dispatch_regressions COMMAND dispatch_tests)
    target_link_libraries(screening_tests PRIVATE qm_compute)
    target_link_libraries(physics_tests PRIVATE qm_compute)
    add_executable(compute_tests tests/ComputeTests.cpp src/QuantumMath.cpp src/RadialSampler.cpp)
    target_link_libraries(compute_tests PRIVATE qm_compute)
    add_test(NAME compute_regressions COMMAND compute_tests)
    add_test(NAME cuda_parity COMMAND compute_tests --cuda)
    set_tests_properties(cuda_parity PROPERTIES SKIP_RETURN_CODE 77)
endif()
