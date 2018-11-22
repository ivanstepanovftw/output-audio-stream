//
// Created by root on 07.11.18.
//

#pragma once

#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <enet/enet.h>
#include <string>
#include <cstring>
#include <ifaddrs.h>
//
#include <streamsome/common.hh>


NAMESPACE_BEGIN(SS)

void
print_ipv4s() {
    char *result;
    char buf[64];
    in_addr *in_addr;
    sockaddr_in *s4;
    //TODO: to research getaddrinfo()
    bool found = false;
    static bool everFound = false;

    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
    }
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        // if (ifa->ifa_addr == NULL || ifa->ifa_flags & IFF_LOOPBACK || !(ifa->ifa_flags & IFF_RUNNING))  // no loopback interfaces or not running
        if (ifa->ifa_addr == NULL || !(ifa->ifa_flags & IFF_UP))
            continue;

        if (ifa->ifa_addr->sa_family == AF_INET) {
            s4 = (sockaddr_in *) ifa->ifa_addr;
            in_addr = &s4->sin_addr;
            if (inet_ntop(ifa->ifa_addr->sa_family, &s4->sin_addr, buf, 0x40u) == 0LL) {
                printf("%s: inet_ntop failed!\n", ifa->ifa_name);
            } else if ((in_addr->s_addr & 0xFF) != 127) {
                if (found == 1) {
                    if (everFound != 1)
                        printf("Possible alternate IP address: %s\n", buf);
                } else {
                    result = new char[64];
                    strcpy(result, buf);
                    found = 1;
                    if (everFound != 1)
                        printf("This server's probable IPv4 address: %s\n", buf);
                }
            }
        }
    }
    freeifaddrs(ifaddr);
    if (found) {
        everFound = 1;
    } else {
        if (everFound != 1)
            puts("Could not determine this server's IPv4 address");
    }
}

std::string getIPAddress(){
    std::string ipAddress = "Unable to get IP Address";
    struct ifaddrs *interfaces = NULL;
    struct ifaddrs *temp_addr = NULL;
    int success = 0;
    // retrieve the current interfaces - returns 0 on success
    success = getifaddrs(&interfaces);
    if (success == 0) {
        // Loop through linked list of interfaces
        temp_addr = interfaces;
        while(temp_addr != NULL) {
            if(temp_addr->ifa_addr->sa_family == AF_INET) {
                // Check if interface is en0 which is the wifi connection on the iPhone
                if(strcmp(temp_addr->ifa_name, "en0") != 0) {
                    ipAddress = inet_ntoa(((struct sockaddr_in*)temp_addr->ifa_addr)->sin_addr);
                }
            }
            temp_addr = temp_addr->ifa_next;
        }
    }
    // Free memory
    freeifaddrs(interfaces);
    return ipAddress;
}

NAMESPACE_END(SS)
