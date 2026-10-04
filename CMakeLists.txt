cmake_minimum_required(VERSION 4.1)
project(ControllerSupport)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
        nosboost_sdk
        GIT_REPOSITORY https://github.com/halilefe4228/NosSDK_-NosBoost-.git
        GIT_TAG main
)
FetchContent_MakeAvailable(nosboost_sdk)

FetchContent_Declare(
        nosboost_modkit
        GIT_REPOSITORY https://github.com/bazizyon/NosBoost-ModKit.git
        GIT_TAG main
        SOURCE_SUBDIR do-not-configure
)
FetchContent_MakeAvailable(nosboost_modkit)

FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG 691b89baae95d56df42a59b031c9d19685b7ab2f
)
FetchContent_MakeAvailable(imgui)

add_library(ControllerSupport SHARED
        ControllerSupport/ControllerSupport.cpp
        ControllerSupport/Game.cpp
        ControllerSupport/Camera.cpp
        ControllerSupport/Input.cpp
        ControllerSupport/Targeting.cpp
        ControllerSupport/Safety.cpp
        ControllerSupport/Panels.cpp
        ControllerSupport/Skills.cpp
        ControllerSupport/Loadouts.cpp
        ControllerSupport/EditMode.cpp
        ControllerSupport/Cast.cpp
        ControllerSupport/Overlay.cpp
        ControllerSupport/Navigator.cpp
        ControllerSupport/resources.rc
        ${nosboost_modkit_SOURCE_DIR}/WidgetKit/WidgetKit.cpp
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
)

target_include_directories(ControllerSupport PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/assets
        ${nosboost_modkit_SOURCE_DIR}/WidgetKit
        ${nosboost_sdk_SOURCE_DIR}/classes
        ${nosboost_sdk_SOURCE_DIR}/classes/enums
        ${nosboost_sdk_SOURCE_DIR}/classes/structs
        ${nosboost_sdk_SOURCE_DIR}/packets
        ${nosboost_sdk_SOURCE_DIR}/packets/sub
        ${imgui_SOURCE_DIR}
)

target_compile_options(ControllerSupport PRIVATE /EHsc /MP)

if(NOT DEFINED ENV{CI})
    set_target_properties(ControllerSupport PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "C:/Program Files (x86)/Nostale/mods"
    )
endif()
