cmake_minimum_required(VERSION 3.20)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(ninjaUSB)

file(GLOB SRC_FILES "src/*.c")

target_sources(app PRIVATE ${SRC_FILES})
