# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(SRCS ${app_sources})

target_add_binary_data(${COMPONENT_TARGET} "private.pem.key" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "certificate.pem.crt" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "aws-root-ca.pem" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "Alsaqr.pem" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "espressif.pem" TEXT)