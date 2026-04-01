set(CORE ${CMAKE_CURRENT_SOURCE_DIR}/JsMaterialXCore/)
set(FORMAT ${CMAKE_CURRENT_SOURCE_DIR}/JsMaterialXFormat/)
set(GENSHADER ${CMAKE_CURRENT_SOURCE_DIR}/JsMaterialXGenShader/)
set(GENESSL ${CMAKE_CURRENT_SOURCE_DIR}/JsMaterialXGenEssl/)
set(GENOSL ${CMAKE_CURRENT_SOURCE_DIR}/JsMaterialXGenOsl/)
set(GENGLSL ${CMAKE_CURRENT_SOURCE_DIR}/JsMaterialXGenGlsl/)
set(GENMSL ${CMAKE_CURRENT_SOURCE_DIR}/JsMaterialXGenMsl/)
set(GENVK ${CMAKE_CURRENT_SOURCE_DIR}/JsMaterialXGenVk/)
set(GENMDL ${CMAKE_CURRENT_SOURCE_DIR}/JsMaterialXGenMdl/)
set(GENWGSL ${CMAKE_CURRENT_SOURCE_DIR}/JsMaterialXGenWgsl/)
set(GENSLANG ${CMAKE_CURRENT_SOURCE_DIR}/JsMaterialXGenSlang/)

set(SOURCE_FOLDER ${PROJECT_SOURCE_DIR}/source)

set(CORE_DEPS ${CORE}JsTypes.cpp
    ${CORE}JsMaterial.cpp
    ${CORE}JsUnit.cpp
    ${CORE}JsExceptions.cpp
    ${CORE}JsUtil.cpp
    ${CORE}JsElement.cpp
    ${CORE}JsInterface.cpp
    ${CORE}JsNode.cpp
    ${CORE}JsDefinition.cpp
    ${CORE}JsDocument.cpp
    ${CORE}JsLook.cpp
    ${CORE}JsProperty.cpp
    ${CORE}JsGeom.cpp
    ${CORE}JsVariant.cpp
    ${CORE}JsValue.cpp
    ${CORE}JsTraversal.cpp)

set(FORMAT_DEPS
    ${FORMAT}JsUtil.cpp
    ${FORMAT}JsXmlIo.cpp
    ${FORMAT}JsEnviron.cpp)

set(GENSHADER_DEPS ${GENSHADER}JsGenContext.cpp
	${GENSHADER}JsGenOptions.cpp
    ${GENSHADER}JsHwShaderGenerator.cpp
    ${GENSHADER}JsShader.cpp
    ${GENSHADER}JsShaderGenerator.cpp
    ${GENSHADER}JsShaderNode.cpp
    ${GENSHADER}JsShaderStage.cpp
    ${GENSHADER}JsTypeDesc.cpp
    ${GENSHADER}JsUtil.cpp)

set(GENESSL_DEPS ${GENESSL}JsEsslShaderGenerator.cpp)
set(GENOSL_DEPS ${GENOSL}JsOslShaderGenerator.cpp)
set(GENGLSL_DEPS ${GENGLSL}JsGlslShaderGenerator.cpp)
set(GENMSL_DEPS ${GENMSL}JsMslShaderGenerator.cpp)
set(GENVK_DEPS ${GENVK}JsVkShaderGenerator.cpp)
set(GENWGSL_DEPS ${GENWGSL}JsWgslShaderGenerator.cpp)
set(GENMDL_DEPS ${GENMDL}JsMdlShaderGenerator.cpp)
set(GENSLANG_DEPS ${GENSLANG}JsSlangShaderGenerator.cpp)

# Linker flags
set(JS_LINK_FLAGS_CORE "")
if (EXTERNAL_LINK_FLAGS)
    string(APPEND JS_LINK_FLAGS_CORE "${EXTERNAL_LINK_FLAGS} ")
endif()

string(APPEND JS_LINK_FLAGS_CORE " -I${SOURCE_FOLDER} \
    -s WASM=1 \
    -s DISABLE_EXCEPTION_CATCHING=0 \
    -s MODULARIZE=1 \
    -s EXPORT_ES6=1 \
    -s EXPORT_NAME='MaterialX' \
    -s MEMORY_GROWTH_LINEAR_STEP=32MB \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s FORCE_FILESYSTEM=1 ")

# Add the post javascript files. The one in the root folder of this project needs to come first.
string(APPEND JS_LINK_FLAGS_CORE "--pre-js ${CMAKE_CURRENT_SOURCE_DIR}/pre.js \
    --post-js ${FORMAT}post.js ")

# Compiler flags
set(JS_COMPILE_FLAGS "")
if (EXTERNAL_COMPILE_FLAGS)
    string(APPEND JS_COMPILE_FLAGS "${EXTERNAL_COMPILE_FLAGS} ")
endif()

if (CMAKE_BUILD_TYPE MATCHES Debug)
    message("JS: Building Debug")
    string(APPEND JS_LINK_FLAGS_CORE "--bind ")
else()
    message("JS: Building Release")
    string(APPEND JS_LINK_FLAGS_CORE "-flto -Os --bind ")
    string(APPEND JS_COMPILE_FLAGS "-flto -Os ")
endif()


set(JS_LINK_FLAGS_GENSHADER "${JS_LINK_FLAGS_CORE} --preload-file ${PROJECT_SOURCE_DIR}/libraries@libraries ")

add_executable(JsMaterialXCore MaterialXLib.cpp
    ${CORE_DEPS}
    ${FORMAT_DEPS})

add_executable(JsMaterialXGenShader MaterialXLib.cpp
    ${CORE_DEPS}
    ${FORMAT_DEPS}
    ${GENSHADER_DEPS}
    ${GENESSL_DEPS})

if (MATERIALX_BUILD_GEN_GLSL)
    message("JS: Building JsMaterialXGenShader with GLSL, VK, WGSL support")
    target_sources(JsMaterialXGenShader PRIVATE ${GENGLSL_DEPS} ${GENVK_DEPS} ${GENWGSL_DEPS})
endif()
if (MATERIALX_BUILD_GEN_OSL)
    message("JS: Building JsMaterialXGenShader with OSL support")
    target_sources(JsMaterialXGenShader PRIVATE ${GENOSL_DEPS})
endif()
if (MATERIALX_BUILD_GEN_MDL)
    message("JS: Building JsMaterialXGenShader with MDL support")
    target_sources(JsMaterialXGenShader PRIVATE ${GENMDL_DEPS})
endif()
if (MATERIALX_BUILD_GEN_MSL)
    message("JS: Building JsMaterialXGenShader with MSL support")
    target_sources(JsMaterialXGenShader PRIVATE ${GENMSL_DEPS})
endif()
if (MATERIALX_BUILD_GEN_SLANG)
    message("JS: Building JsMaterialXGenShader with Slang support")
    target_sources(JsMaterialXGenShader PRIVATE ${GENSLANG_DEPS})
endif()

set_target_properties(JsMaterialXCore
    PROPERTIES
    OUTPUT_NAME JsMaterialXCore
    COMPILE_FLAGS "${JS_COMPILE_FLAGS}"
    LINK_FLAGS "${JS_LINK_FLAGS_CORE}"
    INSTALL_RPATH "${MATERIALX_UP_TWO_RPATH}"
    SOVERSION "${MATERIALX_MAJOR_VERSION}")

set_target_properties(JsMaterialXGenShader
    PROPERTIES
    OUTPUT_NAME JsMaterialXGenShader
    COMPILE_FLAGS "${JS_COMPILE_FLAGS}"
    LINK_FLAGS "${JS_LINK_FLAGS_GENSHADER}"
    INSTALL_RPATH "${MATERIALX_UP_TWO_RPATH}"
    SOVERSION "${MATERIALX_MAJOR_VERSION}")

target_link_libraries(JsMaterialXCore
    PUBLIC MaterialXCore
    PUBLIC MaterialXFormat
    PRIVATE ${CMAKE_DL_LIBS})

target_link_libraries(JsMaterialXGenShader
    PUBLIC MaterialXCore
    PUBLIC MaterialXFormat
    PUBLIC MaterialXGenShader
    PUBLIC MaterialXGenHw
    PUBLIC MaterialXGenGlsl
    PRIVATE ${CMAKE_DL_LIBS})

if (MATERIALX_BUILD_GEN_OSL)
    target_link_libraries(JsMaterialXGenShader PUBLIC MaterialXGenOsl)
endif()
if (MATERIALX_BUILD_GEN_MDL)
    target_link_libraries(JsMaterialXGenShader PRIVATE MaterialXGenMdl)
endif()
if (MATERIALX_BUILD_GEN_MSL)
    target_link_libraries(JsMaterialXGenShader PUBLIC MaterialXGenMsl)
endif()
if (MATERIALX_BUILD_GEN_SLANG)
    target_link_libraries(JsMaterialXGenShader PUBLIC MaterialXGenSlang)
endif()

# Install the JavaScript output
install(TARGETS JsMaterialXCore DESTINATION "JavaScript/MaterialX")
install(TARGETS JsMaterialXGenShader DESTINATION "JavaScript/MaterialX")

# Copy the wasm output
install(FILES ${PROJECT_BINARY_DIR}/bin/JsMaterialXCore.wasm DESTINATION JavaScript/MaterialX/)
install(FILES ${PROJECT_BINARY_DIR}/bin/JsMaterialXGenShader.wasm ${PROJECT_BINARY_DIR}/bin/JsMaterialXGenShader.data DESTINATION JavaScript/MaterialX/)

# Copy the JavaScript files with the version appended to them.
install(FILES ${PROJECT_BINARY_DIR}/bin/JsMaterialXCore.js DESTINATION ${PROJECT_BINARY_DIR}/ RENAME JsMaterialXCore-${MATERIALX_LIBRARY_VERSION}.js)
install(FILES ${CMAKE_INSTALL_PREFIX}/JavaScript/MaterialX/JsMaterialXCore.js DESTINATION JavaScript/MaterialX/ RENAME JsMaterialXCore-${MATERIALX_LIBRARY_VERSION}.js)

install(FILES ${PROJECT_BINARY_DIR}/bin/JsMaterialXGenShader.js DESTINATION ${PROJECT_BINARY_DIR}/ RENAME JsMaterialXGenShader-${MATERIALX_LIBRARY_VERSION}.js)
install(FILES ${CMAKE_INSTALL_PREFIX}/JavaScript/MaterialX/JsMaterialXGenShader.js DESTINATION JavaScript/MaterialX/ RENAME JsMaterialXGenShader-${MATERIALX_LIBRARY_VERSION}.js)

# Option to patch JS files for ES6 module compatibility (needed for Needle Engine)
option(MATERIALX_BUILD_JS_PATCH_FOR_NEEDLE "Patch JS files for ES6 module compatibility (adds export default and fixes arrow function)" OFF)

if(MATERIALX_BUILD_JS_PATCH_FOR_NEEDLE)
    # Generate revision.json and SHA marker at BUILD time (not configure time)
    # so the commit hash and timestamp reflect the actual build, not the last cmake run.
    set(NEEDLE_REVISION_SCRIPT "${PROJECT_BINARY_DIR}/generate_revision.sh")
    file(WRITE ${NEEDLE_REVISION_SCRIPT} "#!/bin/bash\n\
COMMIT=\$(git -C \"${PROJECT_SOURCE_DIR}\" rev-parse HEAD 2>/dev/null || echo unknown)\n\
VERSION=\"${MATERIALX_LIBRARY_VERSION}\"\n\
DATE=\$(date '+%Y-%m-%d %H:%M:%S')\n\
BINDIR=\"${PROJECT_BINARY_DIR}/bin\"\n\
INSTALLDIR=\"${CMAKE_INSTALL_PREFIX}/JavaScript/MaterialX\"\n\
# Remove old SHA files\n\
rm -f \"\$BINDIR\"/SHA_* \"\$INSTALLDIR\"/SHA_* 2>/dev/null\n\
# Write revision.json\n\
for DIR in \"\$BINDIR\" \"\$INSTALLDIR\"; do\n\
  mkdir -p \"\$DIR\"\n\
  printf '{\\n  \"commit\": \"%s\",\\n  \"version\": \"%s\",\\n  \"buildDate\": \"%s\"\\n}\\n' \"\$COMMIT\" \"\$VERSION\" \"\$DATE\" > \"\$DIR/revision.json\"\n\
  touch \"\$DIR/SHA_\$COMMIT\"\n\
done\n\
echo \"Generated revision.json (commit=\$COMMIT, version=\$VERSION, date=\$DATE)\"\n")
    file(CHMOD ${NEEDLE_REVISION_SCRIPT} FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)

    add_custom_target(NeedleRevision ALL
        COMMAND bash ${NEEDLE_REVISION_SCRIPT}
        COMMENT "Generating MaterialX JS revision info"
    )
    add_dependencies(JsMaterialXGenShader NeedleRevision)
    
    set(PATCH_SCRIPT_CONTENT [[
#!/bin/bash
INSTALL_DIR="$1"
if [ -z "$INSTALL_DIR" ]; then
    echo "Usage: $0 <install-directory>"
    exit 1
fi

patch_file() {
    local file="$1"
    if [ -f "$file" ]; then
        echo "Patching $file..."
        # 1. Replace arrow function wrapper with regular function (for ES6 compat)
        sed -i '' 's/var MaterialX = (() => {/var MaterialX = (function() {/' "$file"
        # 2. Replace moduleArg parameter with MaterialX (emscripten 3.x uses moduleArg)
        sed -i '' 's/function(moduleArg = {}) {/function(MaterialX) {\n  MaterialX = MaterialX || {};/' "$file"
        # 3. Replace all remaining moduleArg references with MaterialX
        sed -i '' 's/moduleArg/MaterialX/g' "$file"
        # 4. Add ES6 default export
        if ! grep -q "export default MaterialX;" "$file"; then
            echo "export default MaterialX;" >> "$file"
        fi
    fi
}

patch_file "$INSTALL_DIR/JsMaterialXCore.js"
patch_file "$INSTALL_DIR/JsMaterialXGenShader.js"

# Rename .data to .data.txt so Vite can serve/import it with ?url
if [ -f "$INSTALL_DIR/JsMaterialXGenShader.data" ]; then
    mv "$INSTALL_DIR/JsMaterialXGenShader.data" "$INSTALL_DIR/JsMaterialXGenShader.data.txt"
    echo "Renamed JsMaterialXGenShader.data -> JsMaterialXGenShader.data.txt"
fi

echo "Done patching JS files."
]])
    
    file(WRITE ${PROJECT_BINARY_DIR}/patch_js_for_needle.sh "${PATCH_SCRIPT_CONTENT}")
    file(CHMOD ${PROJECT_BINARY_DIR}/patch_js_for_needle.sh FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    
    install(CODE "
        execute_process(COMMAND ${PROJECT_BINARY_DIR}/patch_js_for_needle.sh \${CMAKE_INSTALL_PREFIX}/JavaScript/MaterialX)
    ")
    
    message(STATUS "MaterialX JS will be patched for Needle Engine compatibility")
endif()
