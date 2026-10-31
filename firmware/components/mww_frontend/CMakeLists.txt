# The audio front end of microWakeWord (TFLite Micro's experimental microfrontend, from pymicro-features:
# the same code the training used to make its features). kissfft itself: TFLite Micro's third_party copy.
file(GLOB srcs "tensorflow/lite/experimental/microfrontend/lib/*.c" "tensorflow/lite/experimental/microfrontend/lib/*.cc")
idf_component_register(SRCS ${srcs} INCLUDE_DIRS "." REQUIRES esp-tflite-micro)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-variable -Wno-error)
