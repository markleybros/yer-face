cmake_minimum_required (VERSION 2.8.2)
project (yer-face)
set(EXECUTABLE_OUTPUT_PATH "bin")

set(CMAKE_C_FLAGS "-Wall -std=c++11 ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS_DEBUG "-g")
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_CXX_FLAGS "-Wall -std=c++11 ${CMAKE_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

include(CTest)

if (CMAKE_VERSION VERSION_LESS 3.2)
    set(UPDATE_DISCONNECTED_IF_AVAILABLE "")
else()
    set(UPDATE_DISCONNECTED_IF_AVAILABLE "UPDATE_DISCONNECTED 1")
endif()

include(DownloadProject.cmake)
download_project(PROJ                googletest
                 GIT_REPOSITORY      https://github.com/google/googletest.git
                 GIT_TAG             master
                 ${UPDATE_DISCONNECTED_IF_AVAILABLE}
)

# Prevent GoogleTest from overriding our compiler/linker options
# when building with Visual Studio
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

add_subdirectory(${googletest_SOURCE_DIR} ${googletest_BINARY_DIR})

# When using CMake 2.8.11 or later, header path dependencies
# are automatically added to the gtest and gmock targets.
# For earlier CMake versions, we have to explicitly add the
# required directories to the header search path ourselves.
if (CMAKE_VERSION VERSION_LESS 2.8.11)
    include_directories("${gtest_SOURCE_DIR}/include"
                        "${gmock_SOURCE_DIR}/include")
endif()

find_package( OpenCV REQUIRED core tracking calib3d )
find_package( dlib REQUIRED )
find_package( SDL2 REQUIRED )

string(STRIP ${SDL2_LIBRARIES} _STRIPPED_SDL2_LIBRARIES)

include_directories("src" ${OpenCV_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS})

add_executable(yer-face src/yer-face.cpp src/Logger.cpp src/SDLDriver.cpp src/FaceTracker.cpp src/FaceMapper.cpp src/MarkerType.cpp src/MarkerTracker.cpp src/MarkerSeparator.cpp src/FrameDerivatives.cpp src/Metrics.cpp src/Utilities.cpp )
target_link_libraries(yer-face ${OpenCV_LIBS} dlib::dlib ${_STRIPPED_SDL2_LIBRARIES})

#Adapted from http://qrikko.blogspot.com/2016/05/cmake-and-how-to-copy-resources-during.html
set (source "${CMAKE_SOURCE_DIR}/data")
set (destination "${CMAKE_CURRENT_BINARY_DIR}/data")
add_custom_command(
 TARGET ${PROJECT_NAME} POST_BUILD
 COMMAND ${CMAKE_COMMAND} -E create_symlink ${source} ${destination}
 DEPENDS ${destination}
 COMMENT "symbolic link data folder from ${source} => ${destination}"
)
