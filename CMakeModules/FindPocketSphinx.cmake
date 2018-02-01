
find_package(PkgConfig)
if (PKG_CONFIG_FOUND)
	pkg_check_modules(POCKETSPHINX pocketsphinx REQUIRED)
endif (PKG_CONFIG_FOUND)

if (POCKETSPHINX_FOUND)
	message(STATUS "Found PocketSphinx: ${POCKETSPHINX_INCLUDE_DIRS} ; ${POCKETSPHINX_LIBRARY_DIRS} ; ${POCKETSPHINX_LIBRARIES}")
else (POCKETSPHINX_FOUND)
	message(FATAL_ERROR "Could not find PocketSphinx")
endif (POCKETSPHINX_FOUND)

