idf_component_register(SRCS "pogoda_espress.c" "time_sync.c"
                    INCLUDE_DIRS "include")
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format")
