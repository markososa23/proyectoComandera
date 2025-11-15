cmake_minimum_required(VERSION 3.15)
project(PrintAgent VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Agregar el ejecutable
add_executable(PrintAgent main.cpp)

# Incluir directorio de headers
target_include_directories(PrintAgent PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Librerías de Windows
if(WIN32)
    target_link_libraries(PrintAgent PRIVATE 
        ws2_32
        winspool
        setupapi
    )
endif()

# Configuración de optimización para Release
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(MSVC)
        target_compile_options(PrintAgent PRIVATE /O2)
    else()
        target_compile_options(PrintAgent PRIVATE -O3)
    endif()
endif()

# Copiar DLLs necesarias (si usas OpenSSL)
if(WIN32)
    # Descomentar si necesitas SSL
    # add_custom_command(TARGET PrintAgent POST_BUILD
    #     COMMAND ${CMAKE_COMMAND} -E copy_if_different
    #         "${CMAKE_SOURCE_DIR}/lib/libssl-1_1-x64.dll"
    #         "${CMAKE_SOURCE_DIR}/lib/libcrypto-1_1-x64.dll"
    #         $<TARGET_FILE_DIR:PrintAgent>
    # )
endif()