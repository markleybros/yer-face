cmake_minimum_required (VERSION 3.8 FATAL_ERROR)
project (yer-face LANGUAGES CXX)

execute_process(COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/ci/version.sh
	RESULT_VARIABLE YERFACE_VERSION_RESULT
	OUTPUT_VARIABLE YERFACE_VERSION
	OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT ${YERFACE_VERSION_RESULT} EQUAL 0)
	message(FATAL_ERROR "Package version string failed to be resolved!")
endif()
message("Package version string: ${YERFACE_VERSION}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/VersionString" "${YERFACE_VERSION}")

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules)


add_definitions(-DYERFACE_VERSION="${YERFACE_VERSION}")

set(CMAKE_C_FLAGS "-Wall ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS_DEBUG "-g")
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_CXX_FLAGS "-Wall ${CMAKE_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

if(WIN32)
# if(MSVC)
# 	add_compile_options("/showIncludes")
# endif()
	set(YERFACE_DATA_DIR "data")
	set(YERFACE_BINDEST_DIR ".")
else(UNIX)
	set(YERFACE_DATA_DIR "share/yer-face")
	set(YERFACE_BINDEST_DIR "bin")
	set(ENV{PKG_CONFIG_PATH} "$ENV{PKG_CONFIG_PATH}:/usr/local/lib/pkgconfig:/opt/local/lib/pkgconfig:/sw/lib/pkgconfig")
	message("Using PKG_CONFIG_PATH:" $ENV{PKG_CONFIG_PATH})
endif()
add_definitions(-DYERFACE_DATA_DIR="${YERFACE_DATA_DIR}")

set( YERFACE_MODULES src/EventLogger.cpp src/FaceDetector.cpp src/FaceMapper.cpp src/FaceTracker.cpp src/FFmpegDriver.cpp src/FrameServer.cpp src/Logger.cpp src/MarkerTracker.cpp src/MarkerType.cpp src/Metrics.cpp src/OutputDriver.cpp src/PreviewHUD.cpp src/SDLDriver.cpp src/SphinxDriver.cpp src/Status.cpp src/Utilities.cpp src/WorkerPool.cpp src/yer-face.cpp )

include(CTest)

set(UPDATE_DISCONNECTED_IF_AVAILABLE "UPDATE_DISCONNECTED 1")

include(CMakeModules/DownloadProject.cmake)
download_project(PROJ	googletest
	URL https://github.com/google/googletest/archive/release-1.11.0.tar.gz
	URL_HASH SHA256=b4870bf121ff7795ba20d20bcdd8627b8e088f2d1dab299a031c1034eddc93d5
	DOWNLOAD_NO_PROGRESS TRUE
	${UPDATE_DISCONNECTED_IF_AVAILABLE}
)

download_project(PROJ	jsoncpp
	URL https://github.com/nlohmann/json/releases/download/v3.10.5/json.hpp
	URL_HASH SHA256=e832d339d9e0c042e7dff807754769d778cf5d6ae9730ce21eed56de99cb5e86
	DOWNLOAD_NO_PROGRESS TRUE
	DOWNLOAD_NO_EXTRACT TRUE
	${UPDATE_DISCONNECTED_IF_AVAILABLE}
)

download_project(PROJ	asio
#	URL https://github.com/chriskohlhoff/asio/archive/asio-1-13-0.tar.gz
#	URL_HASH SHA256=54a1208d20f2104dbd6b7a04a9262f5ab649f4b7a9faf7eac4c2294e9e104c06
	URL https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-21-0.tar.gz
	URL_HASH SHA256=5d2d2dcb7bfb39bff941cabbfc8c27ee322a495470bf0f3a7c5238648cf5e6a9
	DOWNLOAD_NO_PROGRESS TRUE
	${UPDATE_DISCONNECTED_IF_AVAILABLE}
)

download_project(PROJ	websocketpp
	URL https://github.com/zaphoyd/websocketpp/archive/refs/tags/0.8.2.tar.gz
	URL_HASH SHA256=6ce889d85ecdc2d8fa07408d6787e7352510750daa66b5ad44aacb47bea76755
	DOWNLOAD_NO_PROGRESS TRUE
	${UPDATE_DISCONNECTED_IF_AVAILABLE}
)

# Prevent GoogleTest from overriding our compiler/linker options
# when building with Visual Studio
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "Enable installation of googletest.")

add_subdirectory(${googletest_SOURCE_DIR} ${googletest_BINARY_DIR})

include_directories("src" "${jsoncpp_DOWNLOAD_DIR}")

include_directories("src" "${asio_SOURCE_DIR}/asio/include")

include_directories("src" "${websocketpp_SOURCE_DIR}")

find_package( Threads )
find_package( PkgConfig REQUIRED )
find_package( OpenCV 4 REQUIRED COMPONENTS core calib3d imgcodecs )
find_package( dlib REQUIRED )
find_package( SDL2 REQUIRED )
pkg_check_modules(POCKETSPHINX pocketsphinx REQUIRED )
message(STATUS "Found pocketsphinx... INCLUDE_DIRS: ${POCKETSPHINX_INCLUDE_DIRS} LIBRARY_DIRS: ${POCKETSPHINX_LIBRARY_DIRS} LIBRARIES: ${POCKETSPHINX_LIBRARIES} CFLAGS_OTHER: ${POCKETSPHINX_CFLAGS_OTHER} LDFLAGS_OTHER: ${POCKETSPHINX_LDFLAGS_OTHER}")
pkg_check_modules(FFMPEG_AVCODEC libavcodec REQUIRED )
message(STATUS "Found libavcodec... INCLUDE_DIRS: ${FFMPEG_AVCODEC_INCLUDE_DIRS} LIBRARY_DIRS: ${FFMPEG_AVCODEC_LIBRARY_DIRS} LIBRARIES: ${FFMPEG_AVCODEC_LIBRARIES} CFLAGS_OTHER: ${FFMPEG_AVCODEC_CFLAGS_OTHER} LDFLAGS_OTHER: ${FFMPEG_AVCODEC_LDFLAGS_OTHER}")
pkg_check_modules(FFMPEG_AVDEVICE libavdevice REQUIRED )
message(STATUS "Found libavdevice... INCLUDE_DIRS: ${FFMPEG_AVDEVICE_INCLUDE_DIRS} LIBRARY_DIRS: ${FFMPEG_AVDEVICE_LIBRARY_DIRS} LIBRARIES: ${FFMPEG_AVDEVICE_LIBRARIES} CFLAGS_OTHER: ${FFMPEG_AVDEVICE_CFLAGS_OTHER} LDFLAGS_OTHER: ${FFMPEG_AVDEVICE_LDFLAGS_OTHER}")
pkg_check_modules(FFMPEG_AVFILTER libavfilter REQUIRED )
message(STATUS "Found libavfilter... INCLUDE_DIRS: ${FFMPEG_AVFILTER_INCLUDE_DIRS} LIBRARY_DIRS: ${FFMPEG_AVFILTER_LIBRARY_DIRS} LIBRARIES: ${FFMPEG_AVFILTER_LIBRARIES} CFLAGS_OTHER: ${FFMPEG_AVFILTER_CFLAGS_OTHER} LDFLAGS_OTHER: ${FFMPEG_AVFILTER_LDFLAGS_OTHER}")
pkg_check_modules(FFMPEG_AVFORMAT libavformat REQUIRED )
message(STATUS "Found libavformat... INCLUDE_DIRS: ${FFMPEG_AVFORMAT_INCLUDE_DIRS} LIBRARY_DIRS: ${FFMPEG_AVFORMAT_LIBRARY_DIRS} LIBRARIES: ${FFMPEG_AVFORMAT_LIBRARIES} CFLAGS_OTHER: ${FFMPEG_AVFORMAT_CFLAGS_OTHER} LDFLAGS_OTHER: ${FFMPEG_AVFORMAT_LDFLAGS_OTHER}")
pkg_check_modules(FFMPEG_AVUTIL libavutil REQUIRED )
message(STATUS "Found libavutil... INCLUDE_DIRS: ${FFMPEG_AVUTIL_INCLUDE_DIRS} LIBRARY_DIRS: ${FFMPEG_AVUTIL_LIBRARY_DIRS} LIBRARIES: ${FFMPEG_AVUTIL_LIBRARIES} CFLAGS_OTHER: ${FFMPEG_AVUTIL_CFLAGS_OTHER} LDFLAGS_OTHER: ${FFMPEG_AVUTIL_LDFLAGS_OTHER}")
pkg_check_modules(FFMPEG_SWRESAMPLE libswresample REQUIRED )
message(STATUS "Found libswresample... INCLUDE_DIRS: ${FFMPEG_SWRESAMPLE_INCLUDE_DIRS} LIBRARY_DIRS: ${FFMPEG_SWRESAMPLE_LIBRARY_DIRS} LIBRARIES: ${FFMPEG_SWRESAMPLE_LIBRARIES} CFLAGS_OTHER: ${FFMPEG_SWRESAMPLE_CFLAGS_OTHER} LDFLAGS_OTHER: ${FFMPEG_SWRESAMPLE_LDFLAGS_OTHER}")
pkg_check_modules(FFMPEG_SWSCALE libswscale REQUIRED )
message(STATUS "Found libswscale... INCLUDE_DIRS: ${FFMPEG_SWSCALE_INCLUDE_DIRS} LIBRARY_DIRS: ${FFMPEG_SWSCALE_LIBRARY_DIRS} LIBRARIES: ${FFMPEG_SWSCALE_LIBRARIES} CFLAGS_OTHER: ${FFMPEG_SWSCALE_CFLAGS_OTHER} LDFLAGS_OTHER: ${FFMPEG_SWSCALE_LDFLAGS_OTHER}")

 include_directories(
	${CMAKE_CURRENT_BINARY_DIR}
	${OpenCV_INCLUDE_DIRS}
	${POCKETSPHINX_INCLUDE_DIRS}
	${FFMPEG_AVCODEC_INCLUDE_DIRS}
	${FFMPEG_AVDEVICE_INCLUDE_DIRS}
	${FFMPEG_AVFILTER_INCLUDE_DIRS}
	${FFMPEG_AVFORMAT_INCLUDE_DIRS}
	${FFMPEG_AVUTIL_INCLUDE_DIRS}
	${FFMPEG_SWRESAMPLE_INCLUDE_DIRS}
	${FFMPEG_SWSCALE_INCLUDE_DIRS}
)

add_compile_options(
	${POCKETSPHINX_CFLAGS_OTHER}
	${FFMPEG_AVCODEC_CFLAGS_OTHER}
	${FFMPEG_AVDEVICE_CFLAGS_OTHER}
	${FFMPEG_AVFILTER_CFLAGS_OTHER}
	${FFMPEG_AVFORMAT_CFLAGS_OTHER}
	${FFMPEG_AVUTIL_CFLAGS_OTHER}
	${FFMPEG_SWRESAMPLE_CFLAGS_OTHER}
	${FFMPEG_SWSCALE_CFLAGS_OTHER}
)

add_executable( yer-face ${YERFACE_MODULES} )

target_link_directories( yer-face PRIVATE
	${POCKETSPHINX_LIBRARY_DIRS}
	${FFMPEG_AVCODEC_LIBRARY_DIRS}
	${FFMPEG_AVDEVICE_LIBRARY_DIRS}
	${FFMPEG_AVFILTER_LIBRARY_DIRS}
	${FFMPEG_AVFORMAT_LIBRARY_DIRS}
	${FFMPEG_AVUTIL_LIBRARY_DIRS}
	${FFMPEG_SWRESAMPLE_LIBRARY_DIRS}
	${FFMPEG_SWSCALE_LIBRARY_DIRS}
)

target_link_libraries( yer-face
	${OpenCV_LIBS}
	dlib::dlib
	${POCKETSPHINX_LIBRARIES}
	${POCKETSPHINX_LDFLAGS_OTHER}
	${FFMPEG_AVCODEC_LIBRARIES}
	${FFMPEG_AVCODEC_LDFLAGS_OTHER}
	${FFMPEG_AVDEVICE_LIBRARIES}
	${FFMPEG_AVDEVICE_LDFLAGS_OTHER}
	${FFMPEG_AVFILTER_LIBRARIES}
	${FFMPEG_AVFILTER_LDFLAGS_OTHER}
	${FFMPEG_AVFORMAT_LIBRARIES}
	${FFMPEG_AVFORMAT_LDFLAGS_OTHER}
	${FFMPEG_AVUTIL_LIBRARIES}
	${FFMPEG_AVUTIL_LDFLAGS_OTHER}
	${FFMPEG_SWRESAMPLE_LIBRARIES}
	${FFMPEG_SWRESAMPLE_LDFLAGS_OTHER}
	${FFMPEG_SWSCALE_LIBRARIES}
	${FFMPEG_SWSCALE_LDFLAGS_OTHER}
)

if( TARGET SDL2::SDL2 )
	target_link_libraries( yer-face SDL2::SDL2 )
	if( TARGET SDL2::SDL2main )
		target_link_libraries( yer-face SDL2::SDL2main )
	endif()
else()
	string(STRIP ${SDL2_LIBRARIES} _STRIPPED_SDL2_LIBRARIES )
	include_directories(${SDL2_INCLUDE_DIRS})
	target_link_libraries( yer-face ${_STRIPPED_SDL2_LIBRARIES})
endif()

if(MSVC)
	target_link_libraries( yer-face "-NODEFAULTLIB:LIBCMT" )
endif()

target_compile_features( yer-face PUBLIC cxx_std_11 )

if(UNIX)
	#Adapted from http://qrikko.blogspot.com/2016/05/cmake-and-how-to-copy-resources-during.html
	set (YERFACE_DATA_SOURCE "${CMAKE_SOURCE_DIR}/data")
	set (YERFACE_DATA_DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/${YERFACE_DATA_DIR}")
	get_filename_component ( YERFACE_DATA_DESTINATION_DIR ${YERFACE_DATA_DESTINATION} DIRECTORY )
	add_custom_command(
		TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory ${YERFACE_DATA_DESTINATION_DIR}
		COMMENT "make data folder containing directory: ${YERFACE_DATA_DESTINATION_DIR}"
	)
	add_custom_command(
		TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E create_symlink ${YERFACE_DATA_SOURCE} ${YERFACE_DATA_DESTINATION}
		DEPENDS ${YERFACE_DATA_DESTINATION_DIR}
		COMMENT "symbolic link data folder: ${YERFACE_DATA_SOURCE} => ${YERFACE_DATA_DESTINATION}"
	)
endif()

install(TARGETS yer-face RUNTIME
	DESTINATION "${YERFACE_BINDEST_DIR}"
	PERMISSIONS
		OWNER_READ OWNER_WRITE OWNER_EXECUTE
		GROUP_READ GROUP_EXECUTE
		WORLD_READ WORLD_EXECUTE
)
install(DIRECTORY data/ doc
	DESTINATION "${YERFACE_DATA_DIR}"
	FILE_PERMISSIONS
		OWNER_READ OWNER_WRITE
		GROUP_READ
		WORLD_READ
	DIRECTORY_PERMISSIONS
		OWNER_READ OWNER_WRITE OWNER_EXECUTE
		GROUP_READ GROUP_EXECUTE
		WORLD_READ WORLD_EXECUTE
)
