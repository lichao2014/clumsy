project(clumsy C)

file(GLOB SRC src/*.c)
file(GLOB INC src/*.h)
add_executable(clumsy ${SRC} ${INC})

if (MSVC)
    set(IUP_COMPILE_PART dll11)
    set(WIN_DRIVERT_COMPILE_PART MSVC)
elseif(MINGW)
    set(IUP_COMPILE_PART mingw4)
    set(WIN_DRIVERT_COMPILE_PART MINGW)
else()
    message(FATAL "msvc or mingw support")
endif()

set(WIN_DRIVERT_INCLUDE_DIR ${WIN_DRIVERT_DIR}/include)

if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(IUP_PLATFORM_PART Win64)
    set(WIN_DRIVERT_PLATFORM_PART amd64)
else()
    set(IUP_PLATFORM_PART Win32)
    set(WIN_DRIVERT_PLATFORM_PART x86)
endif()

set(WIN_DRIVERT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/WinDivert-1.1.7-${WIN_DRIVERT_COMPILE_PART})
set(WIN_DRIVERT_INCLUDE_DIR ${WIN_DRIVERT_DIR}/include)
set(WIN_DRIVERT_LIBRARY_DIR ${WIN_DRIVERT_DIR}/${WIN_DRIVERT_PLATFORM_PART})
set(IUP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/iup-3.16_${IUP_PLATFORM_PART}_${IUP_COMPILE_PART}_lib)
set(IUP_INCLUDE_DIR ${IUP_DIR}/include)
set(IUP_LIBRARY_DIR ${IUP_DIR})

target_include_directories(
    clumsy
PRIVATE
    ${IUP_INCLUDE_DIR}
    ${WIN_DRIVERT_INCLUDE_DIR}
)

target_link_directories(
    clumsy
PRIVATE
    ${IUP_LIBRARY_DIR}
    ${WIN_DRIVERT_LIBRARY_DIR}
)

target_link_libraries(
    clumsy
PRIVATE
    iup
    WinDivert
    winmm
    ws2_32
)