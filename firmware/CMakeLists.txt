# Only for the "net" build (Arduino as an ESP-IDF component, to change the network buffers):
# the normal builds ignore this file.
cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(oreo)
