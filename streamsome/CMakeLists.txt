project(streamsome LANGUAGES CXX)
find_package(PkgConfig)

# ┌──────────────────────────────────────────────────────────────────┐
#   Create library
# └──────────────────────────────────────────────────────────────────┘
file(GLOB SOURCES "src/*.cc")
add_library(${PROJECT_NAME} SHARED ${SOURCES})

# ┌──────────────────────────────────────────────────────────────────┐
#   Setup public include directory
# └──────────────────────────────────────────────────────────────────┘
set(SS_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(${PROJECT_NAME} PUBLIC
        $<BUILD_INTERFACE:${SS_INCLUDE_DIRS}>
        $<INSTALL_INTERFACE:include>
        PRIVATE src)

# ┌──────────────────────────────────────────────────────────────────┐
#   Linker
# └──────────────────────────────────────────────────────────────────┘
target_include_directories(${PROJECT_NAME} PUBLIC
        ${Boost_INCLUDE_DIRS}
)

# ┌──────────────────────────────────────────────────────────────────┐
#   Install target
# └──────────────────────────────────────────────────────────────────┘
install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib/static
        INCLUDES DESTINATION include
        )

# ┌──────────────────────────────────────────────────────────────────┐
#   Compilation flags
# └──────────────────────────────────────────────────────────────────┘
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
target_compile_options(${PROJECT_NAME} PUBLIC
        -O3
        -fstack-protector-strong
        -fPIC
        -Wall -Wextra -Wpedantic
        -Wformat -Werror=format-security
        -Werror=implicit-function-declaration
        )
target_compile_definitions(${PROJECT_NAME} PUBLIC
        _FORTIFY_SOURCE=2
        )
target_link_options(${PROJECT_NAME} PUBLIC
        -z relro -z now
        -z defs
        -static-libgcc
        -static-libstdc++
        )
target_link_libraries(${PROJECT_NAME}
        -lpulse -lpulse-simple -lpthread -lopus
        -lenet
        ${Boost_LIBRARIES}
        )