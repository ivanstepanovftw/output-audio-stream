project(linux-audiowire-server)

add_executable(linux-audiowire-server
               linux-audiowire-server.cc)

target_link_libraries(linux-audiowire-server
        -lrt -lasound -lpulse -lpulse-simple -lpthread -lportaudio -lopus
        -I/usr/include/gtk-3.0
        -lenet)
