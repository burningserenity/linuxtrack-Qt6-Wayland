cmake_minimum_required(VERSION 3.16)
project(lockd LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(OpenCV REQUIRED COMPONENTS core imgproc objdetect videoio)

# Bundle the haar cascade shipped in src/. It is installed alongside the binary
# and its install path is compiled in so the daemon can find it by default.
set(LOCKD_CASCADE_NAME haarcascade_frontalface_alt2.xml)
set(LOCKD_CASCADE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src/${LOCKD_CASCADE_NAME})
set(LOCKD_CASCADE_INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/share/lockd)

add_executable(lockd
  main.cpp
  locker.cpp
)

target_compile_options(lockd PRIVATE -Wall -Wextra)

target_compile_definitions(lockd PRIVATE
  LOCKD_CASCADE_PATH="${LOCKD_CASCADE_INSTALL_DIR}/${LOCKD_CASCADE_NAME}"
)

target_include_directories(lockd PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(lockd PRIVATE ${OpenCV_LIBS})

install(TARGETS lockd RUNTIME DESTINATION bin)
if(EXISTS ${LOCKD_CASCADE_SRC})
  install(FILES ${LOCKD_CASCADE_SRC} DESTINATION share/lockd)
endif()
