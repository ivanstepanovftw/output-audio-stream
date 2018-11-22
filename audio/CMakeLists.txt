project(streamsome_audio)

set(TARGET 0)

# ┌──────────────────────────────────────────────────────────────────┐
#   server
# └──────────────────────────────────────────────────────────────────┘
set(TARGET ${PROJECT_NAME}_server)
message(STATUS "TARGET=${TARGET}")

add_executable(${TARGET} server/server.cc)
set_property(TARGET ${TARGET} PROPERTY CXX_STANDARD 20)
target_compile_options(${TARGET} PUBLIC
        -O3
        -g3

        -pie -fPIE
        #-O3
        -fstack-protector-strong
        -Wformat -Werror=format-security
        )
target_compile_definitions(${TARGET} PUBLIC
        #NDEBUG

        _FORTIFY_SOURCE=2
        )
target_link_options(${TARGET} PUBLIC
        -z relro -z now
        )
target_link_libraries(${TARGET}
        streamsome
        boost_system
        boost_iostreams
        )


# ┌──────────────────────────────────────────────────────────────────┐
#   LEGACY server
# └──────────────────────────────────────────────────────────────────┘
set(TARGET ${PROJECT_NAME}_server_LEGACY)
message(STATUS "TARGET=${TARGET}")

add_executable(${TARGET} server/server_LEGACY.cc)
set_property(TARGET ${TARGET} PROPERTY CXX_STANDARD 20)
target_compile_options(${TARGET} PUBLIC
        -O0
        -g3

        #-pie -fPIE
        #-O3
        #-fstack-protector-strong
        -Wformat -Werror=format-security
        )
target_compile_definitions(${TARGET} PUBLIC
        #NDEBUG

        #_FORTIFY_SOURCE=2
        )
target_link_options(${TARGET} PUBLIC
        #-z relro -z now
        )
target_link_libraries(${TARGET}
        streamsome
        )


# ┌──────────────────────────────────────────────────────────────────┐
#   client
# └──────────────────────────────────────────────────────────────────┘
set(TARGET ${PROJECT_NAME}_client)
message(STATUS "TARGET=${TARGET}")

add_executable(${TARGET} client/client.cc)
set_property(TARGET ${TARGET} PROPERTY CXX_STANDARD 20)
target_compile_options(${TARGET} PUBLIC
        -O0
        -g3

        #-O3
        #-fstack-protector-strong
        #-pie -fPIE
        -Wformat -Werror=format-security
        )
target_compile_definitions(${TARGET} PUBLIC
#        NDEBUG
#        _FORTIFY_SOURCE=2
        )
target_link_options(${TARGET} PUBLIC
#        -z relro -z now
        )
target_link_libraries(${TARGET}
        streamsome
        ${Boost_LIBRARIES}
        )


# ┌──────────────────────────────────────────────────────────────────┐
#   JNI client
# └──────────────────────────────────────────────────────────────────┘
#set(TARGET ${PROJECT_NAME}_client_android)
#message(STATUS "TARGET=${TARGET}")
#
##set(JAVA_HOME "/usr/lib/jvm/java-10-openjdk")
##find_package(JNI REQUIRED)
#set(ANDROID_INCLUDE_DIRS /home/user/Android/Sdk/ndk-bundle/sysroot/usr/include)
#
#
#add_executable(${TARGET} jni_client/jni_client.cc)
#set_property(TARGET ${TARGET} PROPERTY CXX_STANDARD 20)
#target_include_directories(${TARGET} PUBLIC
#        ${JNI_INCLUDE_DIRS}
#        ${ANDROID_INCLUDE_DIRS}
#        )
#target_compile_options(${TARGET} PUBLIC
#        -O0
#        -g3
#
#        #-O3
#        #-fstack-protector-strong
#        #-pie -fPIE
#        -Wformat -Werror=format-security
#        )
#target_compile_definitions(${TARGET} PUBLIC
##        NDEBUG
#
##        _FORTIFY_SOURCE=2
#        )
#target_link_options(${TARGET} PUBLIC
##        -z relro -z now
#        )
#target_link_libraries(${TARGET}
#        streamsome
#        android
#        log
#        )


# ┌──────────────────────────────────────────────────────────────────┐
#   test
# └──────────────────────────────────────────────────────────────────┘
set(TARGET ${PROJECT_NAME}_TEST)
message(STATUS "TARGET=${TARGET}")

add_executable(${TARGET} test.cc)
set_property(TARGET ${TARGET} PROPERTY CXX_STANDARD 20)
target_compile_options(${TARGET} PUBLIC
        -O3
        -g3

        #-O3
        #-fstack-protector-strong
        #-pie -fPIE
        -Wformat -Werror=format-security
        )
#/usr/lib/jvm/java-10-openjdk/include/
target_compile_definitions(${TARGET} PUBLIC
        #        NDEBUG
        #        _FORTIFY_SOURCE=2
        )
target_link_options(${TARGET} PUBLIC
        #        -z relro -z now
        )
target_link_libraries(${TARGET}
        streamsome
        boost_iostreams
        )


# ┌──────────────────────────────────────────────────────────────────┐
#   test
# └──────────────────────────────────────────────────────────────────┘
set(TARGET ${PROJECT_NAME}_pulseaudio-device-list)
message(STATUS "TARGET=${TARGET}")

add_executable(${TARGET} pulseaudio-device-list.cc)
set_property(TARGET ${TARGET} PROPERTY CXX_STANDARD 20)
target_compile_options(${TARGET} PUBLIC
        -O3
        -g3

        #-O3
        #-fstack-protector-strong
        #-pie -fPIE
        -Wformat -Werror=format-security
        )
#/usr/lib/jvm/java-10-openjdk/include/
target_compile_definitions(${TARGET} PUBLIC
        #        NDEBUG
        #        _FORTIFY_SOURCE=2
        )
target_link_options(${TARGET} PUBLIC
        #        -z relro -z now
        )
target_link_libraries(${TARGET}
        streamsome
        boost_iostreams
        )

