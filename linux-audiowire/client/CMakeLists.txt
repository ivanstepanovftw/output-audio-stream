project(linux-audiowire-client)

add_executable(linux-audiowire-client
               linux-audiowire-client.cc)

target_link_libraries(linux-audiowire-client
        -lrt -lasound -lpulse -lpulse-simple -lpthread -lportaudio -lopus
        -I/usr/include/gtk-3.0
        -lenet)
