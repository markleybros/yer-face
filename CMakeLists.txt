cmake_minimum_required (VERSION 3.8 FATAL_ERROR)
project (yer-face LANGUAGES CXX CUDA)
set(EXECUTABLE_OUTPUT_PATH "bin")

set(CMAKE_C_FLAGS "-Wall ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS_DEBUG "-g")
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_CXX_FLAGS "-Wall ${CMAKE_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

option(USE_CUDA "Use CUDA, default to true" 1)

set( YERFACE_MODULES src/yer-face.cpp src/Logger.cpp src/SDLDriver.cpp src/FaceTracker.cpp src/FaceMapper.cpp src/MarkerType.cpp src/MarkerTracker.cpp src/MarkerSeparator.cpp src/FrameDerivatives.cpp src/Metrics.cpp src/Utilities.cpp )

include(CTest)

set(UPDATE_DISCONNECTED_IF_AVAILABLE "UPDATE_DISCONNECTED 1")

include(DownloadProject.cmake)
download_project(PROJ	googletest
	GIT_REPOSITORY	https://github.com/google/googletest.git
	GIT_TAG	master
	${UPDATE_DISCONNECTED_IF_AVAILABLE}
)

# Prevent GoogleTest from overriding our compiler/linker options
# when building with Visual Studio
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

add_subdirectory(${googletest_SOURCE_DIR} ${googletest_BINARY_DIR})

include_directories("src")

if(USE_CUDA)
	add_definitions(-DHAVE_CUDA)
	find_package( OpenCV REQUIRED core tracking calib3d cudaimgproc )
	add_library( yer-face-cuda STATIC src/GPUUtils.cu )
else()
	find_package( OpenCV REQUIRED core tracking calib3d )
	unset(CUDA_FOUND)
endif()

find_package( dlib REQUIRED )
find_package( SDL2 REQUIRED )

string(STRIP ${SDL2_LIBRARIES} _STRIPPED_SDL2_LIBRARIES)

include_directories(${OpenCV_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR})

add_executable( yer-face ${YERFACE_MODULES} )

target_link_libraries( yer-face ${OpenCV_LIBS} dlib::dlib ${_STRIPPED_SDL2_LIBRARIES})

target_compile_features( yer-face PUBLIC cxx_std_11 )

if( CUDA_FOUND )
	target_link_libraries( yer-face yer-face-cuda )
	set_target_properties( yer-face PROPERTIES CUDA_SEPARABLE_COMPILATION ON)
endif()

#Adapted from http://qrikko.blogspot.com/2016/05/cmake-and-how-to-copy-resources-during.html
set (source "${CMAKE_SOURCE_DIR}/data")
set (destination "${CMAKE_CURRENT_BINARY_DIR}/data")
add_custom_command(
	TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E create_symlink ${source} ${destination}
	DEPENDS ${destination}
	COMMENT "symbolic link data folder from ${source} => ${destination}"
)
