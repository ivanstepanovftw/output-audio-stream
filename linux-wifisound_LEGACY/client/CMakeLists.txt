project(linux-wifisound-client)

add_executable(linux-wifisound-client
               linux-wifisound-client.cc)

target_link_libraries(linux-wifisound-client
        -lrt -lasound -lpulse -lpulse-simple -lpthread -lportaudio -lopus
        -I/usr/include/gtk-3.0
        -lenet)
