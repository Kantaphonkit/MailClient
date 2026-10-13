# CMakeList.txt : CMake project for MailClient, include source and define
# project specific logic here.
#
cmake_minimum_required(VERSION 3.16)
project(MailClient LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(MailClient
    src/main.cpp
    src/TcpConnection.cpp
    src/Logger.cpp
    src/MailMessage.cpp
    src/SmtpClient.cpp
    src/Pop3Client.cpp
    src/Config.cpp
    src/Mailbox.cpp
    src/MailService.cpp
    src/Cli.cpp
    src/Account.cpp
)

 configure_file(config/client.conf ${CMAKE_CURRENT_BINARY_DIR}/config/client.conf COPYONLY)

if(WIN32)
    target_link_libraries(MailClient PRIVATE ws2_32)
    target_compile_definitions(MailClient PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0601)
endif()

if(MSVC)
    target_compile_options(MailClient PRIVATE /W4 /utf-8)
else()
    target_compile_options(MailClient PRIVATE -Wall -Wextra)
endif()

# TODO: Add tests and install targets if needed.
