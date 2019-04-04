cmake_minimum_required (VERSION 3.8 FATAL_ERROR)
project (yer-face LANGUAGES CXX)

execute_process(COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/ci/version.sh
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

set(YERFACE_DATA_DIR "share/yer-face")

add_definitions(-DYERFACE_DATA_DIR="${YERFACE_DATA_DIR}" -DYERFACE_VERSION="${YERFACE_VERSION}")

set(CMAKE_C_FLAGS "-Wall ${CMAKE_C_FLAGS}")
set(CMAKE_C_FLAGS_DEBUG "-g")
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_CXX_FLAGS "-Wall ${CMAKE_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

set(ENV{PKG_CONFIG_PATH} "$ENV{PKG_CONFIG_PATH}:/usr/local/lib/pkgconfig:/opt/local/lib/pkgconfig:/sw/lib/pkgconfig")

set( YERFACE_MODULES src/EventLogger.cpp src/FaceDetector.cpp src/FaceMapper.cpp src/FaceTracker.cpp src/FFmpegDriver.cpp src/FrameServer.cpp src/ImageSequence.cpp src/Logger.cpp src/MarkerTracker.cpp src/MarkerType.cpp src/Metrics.cpp src/OutputDriver.cpp src/PreviewHUD.cpp src/SDLDriver.cpp src/SphinxDriver.cpp src/Status.cpp src/Utilities.cpp src/WorkerPool.cpp src/yer-face.cpp )

include(CTest)

set(UPDATE_DISCONNECTED_IF_AVAILABLE "UPDATE_DISCONNECTED 1")

include(CMakeModules/DownloadProject.cmake)
download_project(PROJ	googletest
	URL https://github.com/google/googletest/archive/release-1.8.1.tar.gz
	URL_HASH SHA256=9bf1fe5182a604b4135edc1a425ae356c9ad15e9b23f9f12a02e80184c3a249c
	DOWNLOAD_NO_PROGRESS TRUE
	${UPDATE_DISCONNECTED_IF_AVAILABLE}
)

download_project(PROJ	jsoncpp
	URL https://github.com/nlohmann/json/releases/download/v3.6.1/json.hpp
	URL_HASH SHA256=d2eeb25d2e95bffeb08ebb7704cdffd2e8fca7113eba9a0b38d60a5c391ea09a
	DOWNLOAD_NO_PROGRESS TRUE
	DOWNLOAD_NO_EXTRACT TRUE
	${UPDATE_DISCONNECTED_IF_AVAILABLE}
)

download_project(PROJ	asio
#	URL https://github.com/chriskohlhoff/asio/archive/asio-1-13-0.tar.gz
#	URL_HASH SHA256=54a1208d20f2104dbd6b7a04a9262f5ab649f4b7a9faf7eac4c2294e9e104c06
	URL https://github.com/chriskohlhoff/asio/archive/asio-1-12-2.tar.gz
	URL_HASH SHA256=1de23266b956674e766cd0b6c929a11259f2284ea8e96b765cc8c67b1689e0fd
	DOWNLOAD_NO_PROGRESS TRUE
	${UPDATE_DISCONNECTED_IF_AVAILABLE}
)

download_project(PROJ	websocketpp
	URL https://github.com/zaphoyd/websocketpp/archive/0.8.1.tar.gz
	URL_HASH SHA256=178899de48c02853b55b1ea8681599641cedcdfce59e56beaff3dd0874bf0286
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

find_package( OpenCV 4 REQUIRED COMPONENTS core calib3d imgcodecs )
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

install(TARGETS yer-face RUNTIME
	DESTINATION bin
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
