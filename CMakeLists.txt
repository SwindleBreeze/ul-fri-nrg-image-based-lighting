cmake_minimum_required(VERSION 3.22)
project(NRG_IBL_Seminar)

set(CMAKE_CXX_STANDARD 17)

include(FetchContent)

find_package(Threads REQUIRED)

set(DAWN_INSTALL_PREFIX "C:/libs/dawn/install/Release" CACHE PATH "Path to Dawn install prefix")
list(PREPEND CMAKE_PREFIX_PATH "${DAWN_INSTALL_PREFIX}")

find_package(Dawn REQUIRED)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
)
FetchContent_MakeAvailable(stb)

FetchContent_Declare(
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG v2.9.3
)
FetchContent_MakeAvailable(tinygltf)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG master
)
FetchContent_MakeAvailable(imgui)

add_executable(Renderer
    main.cpp
    app/BakePipeline.cpp
    app/EvaluationLog.cpp
    io/HdrLoader.cpp
    io/GltfLoader.cpp
    io/Screenshot.cpp
    utils/QueueSync.cpp
    ibl/IblBaker.cpp
    ibl/IblSettings.cpp
    gfx/PbrRenderer.cpp
    gfx/OrbitCamera.cpp
    gfx/UiLayer.cpp
    scene/Scene.cpp
    mesh/SphereMesh.cpp
    mesh/CubeMesh.cpp
    mesh/PlaneMesh.cpp
    utils/WgslUtils.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp
)

target_include_directories(Renderer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${DAWN_INSTALL_PREFIX}/include"
    "C:/libs/dawn/third_party/glfw3/src/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/glm"
    "${CMAKE_CURRENT_SOURCE_DIR}/gfx"
    "${CMAKE_CURRENT_SOURCE_DIR}/app"
    "${CMAKE_CURRENT_SOURCE_DIR}/io"
    "${CMAKE_CURRENT_SOURCE_DIR}/ibl"
    "${CMAKE_CURRENT_SOURCE_DIR}/scene"
    "${CMAKE_CURRENT_SOURCE_DIR}/mesh"
    "${CMAKE_CURRENT_SOURCE_DIR}/utils"
    ${stb_SOURCE_DIR}
    ${tinygltf_SOURCE_DIR}
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_compile_definitions(Renderer PRIVATE
    TINYGLTF_NO_INCLUDE_STB_IMAGE
    TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
    TINYGLTF_NO_STB_IMAGE_WRITE
    IMGUI_IMPL_WEBGPU_BACKEND_DAWN
)

target_link_libraries(Renderer PRIVATE
    dawn::webgpu_dawn
    "C:/libs/dawn/out/Release/third_party/glfw3/src/src/Release/glfw3.lib"
    user32
    gdi32
    shell32
    winmm
)

add_custom_command(TARGET Renderer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/shaders
    $<TARGET_FILE_DIR:Renderer>/shaders
)

add_custom_command(TARGET Renderer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/D3DCompiler_47.dll
    $<TARGET_FILE_DIR:Renderer>/D3DCompiler_47.dll
)
