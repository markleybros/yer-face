cmake_minimum_required (VERSION 3.8 FATAL_ERROR)
project (yer-face LANGUAGES CXX)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules)

set(EXECUTABLE_OUTPUT_PATH "bin")

set(CMAKE_C_FLAGS "-Wall ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS_DEBUG "-g")
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_CXX_FLAGS "-Wall ${CMAKE_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

set(ENV{PKG_CONFIG_PATH} "$ENV{PKG_CONFIG_PATH}:/usr/local/lib/pkgconfig:/opt/local/lib/pkgconfig:/sw/lib/pkgconfig")

option(USE_CUDA "Use CUDA, default to true" 1)

set( YERFACE_MODULES src/yer-face.cpp src/Logger.cpp src/SDLDriver.cpp src/FFmpegDriver.cpp src/SphinxDriver.cpp src/OutputDriver.cpp src/FaceTracker.cpp src/FaceMapper.cpp src/MarkerType.cpp src/MarkerTracker.cpp src/MarkerSeparator.cpp src/FrameDerivatives.cpp src/Metrics.cpp src/Utilities.cpp )

include(CTest)

set(UPDATE_DISCONNECTED_IF_AVAILABLE "UPDATE_DISCONNECTED 1")

include(CMakeModules/DownloadProject.cmake)
download_project(PROJ	googletest
	GIT_REPOSITORY	https://github.com/google/googletest.git
	GIT_TAG	master
	${UPDATE_DISCONNECTED_IF_AVAILABLE}
)

download_project(PROJ	jsoncpp
	GIT_REPOSITORY	https://github.com/nlohmann/json.git
	GIT_TAG	master
	${UPDATE_DISCONNECTED_IF_AVAILABLE}
)

download_project(PROJ	asio
	GIT_REPOSITORY	https://github.com/chriskohlhoff/asio.git
	GIT_TAG	master
	${UPDATE_DISCONNECTED_IF_AVAILABLE}
)

download_project(PROJ	websocketpp
	GIT_REPOSITORY	https://github.com/zaphoyd/websocketpp.git
	GIT_TAG	master
	${UPDATE_DISCONNECTED_IF_AVAILABLE}
)

# Prevent GoogleTest from overriding our compiler/linker options
# when building with Visual Studio
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

add_subdirectory(${googletest_SOURCE_DIR} ${googletest_BINARY_DIR})

include_directories("src" "${jsoncpp_SOURCE_DIR}/single_include/nlohmann")

include_directories("src" "${asio_SOURCE_DIR}/asio/include")

include_directories("src" "${websocketpp_SOURCE_DIR}")

set( _OPENCV_REQUIRED_MODULES core tracking calib3d )

if(USE_CUDA)
	enable_language(CUDA)
	list(APPEND _OPENCV_REQUIRED_MODULES cudaimgproc cudafilters )
	add_definitions(-DHAVE_CUDA)
	add_library( yer-face-cuda STATIC src/GPUUtils.cu )
else()
	unset(CUDA_FOUND)
endif()

find_package( OpenCV 4 REQUIRED COMPONENTS ${_OPENCV_REQUIRED_MODULES} )
find_package( dlib REQUIRED )
find_package( SDL2 REQUIRED )
find_package( FFmpeg REQUIRED )
find_package( PocketSphinx REQUIRED )

string(STRIP ${SDL2_LIBRARIES} _STRIPPED_SDL2_LIBRARIES )

include_directories(${CMAKE_CURRENT_BINARY_DIR} ${OpenCV_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS} ${FFMPEG_INCLUDE_DIR} ${POCKETSPHINX_INCLUDE_DIRS} )

link_directories(${POCKETSPHINX_LIBRARY_DIRS})

add_executable( yer-face ${YERFACE_MODULES} )

target_link_libraries( yer-face ${OpenCV_LIBS} dlib::dlib ${_STRIPPED_SDL2_LIBRARIES} ${FFMPEG_LIBAVCODEC} ${FFMPEG_LIBAVFORMAT} ${FFMPEG_LIBAVDEVICE} ${FFMPEG_LIBAVUTIL} ${FFMPEG_LIBSWSCALE} ${FFMPEG_LIBSWRESAMPLE} ${POCKETSPHINX_LIBRARIES} )

target_compile_features( yer-face PUBLIC cxx_std_11 )

if( USE_CUDA )
	target_link_libraries( yer-face yer-face-cuda )
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
