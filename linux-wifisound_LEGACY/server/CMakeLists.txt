project(linux-wifisound-server)

add_executable(linux-wifisound-server
               linux-wifisound-server.cc)

target_link_libraries(linux-wifisound-server
        -lrt -lasound -lpulse -lpulse-simple -lpthread -lportaudio -lopus
        -I/usr/include/gtk-3.0
        -lenet)
