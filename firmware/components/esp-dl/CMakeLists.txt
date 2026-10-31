# esp-dl (face detection) for the "net" build: the same prebuilt headers + libraries that
# Arduino 2.0.17 ships (tools/sdk/esp32s3), so face detection is identical to the normal build.
idf_component_register(INCLUDE_DIRS include include/detect include/image include/layer include/math
                                    include/model_zoo include/nn include/tool include/typedef)
foreach(l human_face_detect cat_face_detect color_detect dl mfn)
  target_link_libraries(${COMPONENT_LIB} INTERFACE "${CMAKE_CURRENT_LIST_DIR}/lib/lib${l}.a")
endforeach()
