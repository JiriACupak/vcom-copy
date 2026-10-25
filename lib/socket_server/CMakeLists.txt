cmake_minimum_required(VERSION 3.15)

project(socketserver)

add_library(socketserver "src/socket_server.c")

target_include_directories(socketserver
    PUBLIC "include"
    PRIVATE "src"
)

target_link_libraries(socketserver csp)

