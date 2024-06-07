cmake_minimum_required(VERSION 3.28)

project(hlsaves LANGUAGES C)

add_executable(
  ${PROJECT_NAME}
    src/hlsaves.c
    src/oodle.c
)

target_include_directories(
  ${PROJECT_NAME}
    PRIVATE
    headers/
)

target_precompile_headers(
  ${PROJECT_NAME}
    PUBLIC
    headers/common.h
)

add_custom_command(
  TARGET ${PROJECT_NAME}
  POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> ${CMAKE_CURRENT_LIST_DIR}/
  COMMENT "Copying ${PROJECT_NAME} to install directory"
)
