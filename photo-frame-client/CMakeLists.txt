# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Default to 'dev' if not specified
if(NOT DEFINED APP_ENV)
    set(APP_ENV "dev")
endif()
# Order matters: sdkconfig.defaults loads first, then sdkconfig.${APP_ENV} overrides it
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.${APP_ENV}")

# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(photo-frame-client)
