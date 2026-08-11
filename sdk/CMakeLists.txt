cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project(XScope LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(XSCOPE_BUILD_SMOKE "Build SDK smoke executable" ON)

set(XSCOPE_XAIOP_GO_SRC "${CMAKE_SOURCE_DIR}/../dev/XAIOP/xaiop-sdk/go" CACHE PATH
    "Path to XAIOP Go SDK module (github.com/AboutUip/XAIOP/xaiop-sdk/go)")
set(XSCOPE_XAIOP_NATIVE_DIR "${CMAKE_SOURCE_DIR}/deps/xaiop_native")
set(XSCOPE_XAIOP_NATIVE_OUT "${XSCOPE_XAIOP_NATIVE_DIR}/build")

find_program(GO_EXECUTABLE go REQUIRED)

# --- Embedded Go XAIOP (c-shared) ---
set(XSCOPE_XAIOP_DLL "${XSCOPE_XAIOP_NATIVE_OUT}/xaiop_native.dll")
set(XSCOPE_XAIOP_HDR "${XSCOPE_XAIOP_NATIVE_OUT}/xaiop_native.h")

add_custom_command(
  OUTPUT "${XSCOPE_XAIOP_DLL}" "${XSCOPE_XAIOP_HDR}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${XSCOPE_XAIOP_NATIVE_OUT}"
  COMMAND ${CMAKE_COMMAND} -E env CGO_ENABLED=1
          ${GO_EXECUTABLE} build -buildmode=c-shared
          -o "${XSCOPE_XAIOP_DLL}" .
  WORKING_DIRECTORY "${XSCOPE_XAIOP_NATIVE_DIR}"
  DEPENDS
    "${XSCOPE_XAIOP_NATIVE_DIR}/main.go"
    "${XSCOPE_XAIOP_NATIVE_DIR}/go.mod"
  COMMENT "Building embedded XAIOP Go native library"
  VERBATIM
)

add_custom_target(xaiop_native DEPENDS "${XSCOPE_XAIOP_DLL}")

# --- SQLite amalgamation ---
add_library(xscope_sqlite STATIC
  deps/sqlite/sqlite3.c
)
target_include_directories(xscope_sqlite PUBLIC deps/sqlite)
target_compile_definitions(xscope_sqlite PUBLIC
  SQLITE_THREADSAFE=1
  SQLITE_DQS=0
  SQLITE_DEFAULT_MEMSTATUS=0
  SQLITE_OMIT_LOAD_EXTENSION
)
if(MSVC)
  target_compile_options(xscope_sqlite PRIVATE /W0)
else()
  target_compile_options(xscope_sqlite PRIVATE -w)
endif()

# --- XScope core library ---
add_library(xscope_core STATIC
  src/storage/database.cpp
  src/storage/workspace.cpp
  src/crypto/secret_box_win.cpp
  src/xaiop/bridge.cpp
  src/xaiop/live_parser.cpp
  src/network/types.cpp
  src/network/http_client_win.cpp
  src/network/xaiop_session.cpp
  src/skills/skill_store.cpp
  src/registry/search_module.cpp
  src/registry/search_registry.cpp
  src/registry/usable_module.cpp
  src/prompts/prompt_engine.cpp
  src/mcp/search_tools.cpp
  src/auth/token_store.cpp
  src/auth/device_flow.cpp
  src/auth/github_oauth.cpp
  src/providers/github/rest_client.cpp
  src/providers/github/builtin.cpp
  src/providers/bocha/client.cpp
  src/providers/bocha/builtin.cpp
  src/ai/types.cpp
  src/ai/provider_registry.cpp
  src/ai/openai_compatible.cpp
  src/ai/runtime.cpp
  src/ai/builtin.cpp
  src/research/precision.cpp
  src/research/types.cpp
  src/research/evidence_store.cpp
  src/research/knowledge_graph.cpp
  src/research/memory_tree.cpp
  src/research/orchestrator.cpp
  src/utils/string.cpp
  src/utils/path.cpp
  src/utils/time.cpp
  src/utils/json.cpp
  src/utils/url.cpp
  src/utils/base64.cpp
)
add_dependencies(xscope_core xaiop_native)
target_include_directories(xscope_core
  PUBLIC
    ${CMAKE_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_SOURCE_DIR}/deps/sqlite
)
target_link_libraries(xscope_core PUBLIC xscope_sqlite)
if(WIN32)
  target_link_libraries(xscope_core PRIVATE bcrypt crypt32 winhttp shell32)
endif()
if(MSVC)
  target_compile_options(xscope_core PRIVATE /utf-8)
endif()

# --- C API shared library (WPF / PInvoke) ---
option(XSCOPE_BUILD_CAPI "Build xscope_capi shared library" ON)
if(XSCOPE_BUILD_CAPI)
  add_library(xscope_capi SHARED
    src/capi/xscope_capi.cpp
  )
  target_compile_definitions(xscope_capi PRIVATE XSCOPE_CAPI_EXPORTS)
  target_link_libraries(xscope_capi PRIVATE xscope_core)
  target_include_directories(xscope_capi PUBLIC ${CMAKE_SOURCE_DIR}/include)
  if(MSVC)
    target_compile_options(xscope_capi PRIVATE /utf-8)
  endif()
  add_custom_command(TARGET xscope_capi POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${XSCOPE_XAIOP_DLL}"
            "$<TARGET_FILE_DIR:xscope_capi>/xaiop_native.dll"
    COMMENT "Copy xaiop_native.dll next to xscope_capi"
  )
endif()

# --- Smoke test / demo entry ---
if(XSCOPE_BUILD_SMOKE)
  add_executable(XScope src/main.cpp)
  target_link_libraries(XScope PRIVATE xscope_core)
  if(MSVC)
    target_compile_options(XScope PRIVATE /utf-8)
  endif()
  add_custom_command(TARGET XScope POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${XSCOPE_XAIOP_DLL}"
            "$<TARGET_FILE_DIR:XScope>/xaiop_native.dll"
    COMMENT "Copy xaiop_native.dll next to XScope"
  )
endif()
