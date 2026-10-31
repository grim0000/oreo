# The "net" build (see ../CMakeLists.txt): the robot's code is main.cpp and the headers it includes.
idf_component_register(SRCS "main.cpp" INCLUDE_DIRS ".")
# Warnings stay warnings, as in the normal Arduino build (ESP-IDF makes some of them errors)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error -mtext-section-literals)  # (main.cpp + its headers grew past l32r reach: "dangerous relocation", 2026-10-19)
