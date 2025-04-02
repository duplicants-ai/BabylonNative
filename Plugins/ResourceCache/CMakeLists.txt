set(SOURCES
    "Include/Babylon/Plugins/ResourceCache.h"
    "Source/ResourceCache.cpp"
)

set(JS_SOURCES
    "Source/ResourceCache.js"
)

add_library(ResourceCache ${SOURCES})

target_include_directories(ResourceCache
    PUBLIC "Include"
)

target_link_libraries(ResourceCache
    PUBLIC napi
    PRIVATE arcana
    PRIVATE JsRuntime
    PRIVATE ScriptLoader
)

# Copy JavaScript files to the Scripts directory in the build output
foreach(JS_FILE ${JS_SOURCES})
    get_filename_component(JS_FILENAME ${JS_FILE} NAME)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/${JS_FILE}
        ${CMAKE_BINARY_DIR}/Scripts/${JS_FILENAME}
        COPYONLY
    )
endforeach()

warnings_as_errors(ResourceCache)

set_property(TARGET ResourceCache PROPERTY FOLDER Plugins)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES} ${JS_SOURCES})
