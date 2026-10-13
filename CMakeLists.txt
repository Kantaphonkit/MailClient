cmake_minimum_required(VERSION 3.16)
project(MailClient LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- mailcore: everything except the user interface (shared by CLI and GUI)
add_library(mailcore STATIC
    src/TcpConnection.cpp
    src/Logger.cpp
    src/MailMessage.cpp
    src/SmtpClient.cpp
    src/Pop3Client.cpp
    src/Config.cpp
    src/Mailbox.cpp
    src/MailService.cpp
    src/Account.cpp
)
target_include_directories(mailcore PUBLIC src)
if(WIN32)
    target_link_libraries(mailcore PUBLIC ws2_32)
    target_compile_definitions(mailcore PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0601)
endif()

# ---- MailClient: the command-line client
add_executable(MailClient src/main.cpp src/Cli.cpp)
target_link_libraries(MailClient PRIVATE mailcore)

set(OUR_TARGETS mailcore MailClient)

# ---- MailClientGui: the graphical client (Windows only: Win32 + Direct3D 11 + Dear ImGui)
if(WIN32)
    set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
    add_library(imgui STATIC
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/imgui_stdlib.cpp
        ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
        ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    )
    target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
    target_link_libraries(imgui PUBLIC d3d11 dxgi d3dcompiler dwmapi imm32)

    add_executable(MailClientGui WIN32 src/gui/main_gui.cpp src/gui/GuiApp.cpp)
    target_link_libraries(MailClientGui PRIVATE mailcore imgui)
    list(APPEND OUR_TARGETS MailClientGui)
endif()

# Warnings only for our own code, not for third-party code
foreach(t ${OUR_TARGETS})
    if(MSVC)
        target_compile_options(${t} PRIVATE /W4 /utf-8)
    else()
        target_compile_options(${t} PRIVATE -Wall -Wextra)
    endif()
endforeach()
if(MSVC)
    target_compile_options(imgui PRIVATE /utf-8)
endif()

# The programs read config/client.conf relative to the build folder
configure_file(config/client.conf ${CMAKE_CURRENT_BINARY_DIR}/config/client.conf COPYONLY)