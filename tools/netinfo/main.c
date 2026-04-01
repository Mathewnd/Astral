#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>

static void print_error(const char *msg, const int error) {
        fprintf(stderr, "%s: %s: %s\n", program_invocation_short_name, msg, strerror(error));
}

int main(const int argc, const char *argv[]) {
        struct ifaddrs *ifaddrs;
        if (getifaddrs(&ifaddrs)) {
                print_error("getifaddrs", errno);
                return EXIT_FAILURE;
        }

        for (struct ifaddrs *ifaddr = ifaddrs; ifaddr; ifaddr = ifaddr->ifa_next) {
		if (ifaddr->ifa_addr->sa_family != AF_INET) 
			continue;

                printf("%s: flags: 0x%x%s%s%s\n",
			ifaddr->ifa_name, ifaddr->ifa_flags,
			ifaddr->ifa_flags & IFF_UP       ? " UP"        : "",
			ifaddr->ifa_flags & IFF_LOOPBACK ? " LOOPBACK"  : "",
			ifaddr->ifa_flags & IFF_RUNNING  ? " RUNNING"   : "");
                printf("%s: address: %s\n", ifaddr->ifa_name, ifaddr->ifa_addr ? inet_ntoa(((struct sockaddr_in *)ifaddr->ifa_addr)->sin_addr) : "?");
                printf("%s: mask: %s\n", ifaddr->ifa_name, ifaddr->ifa_netmask ? inet_ntoa(((struct sockaddr_in *)ifaddr->ifa_netmask)->sin_addr) : "?");
                printf("%s: broadcast: %s\n", ifaddr->ifa_name, ifaddr->ifa_broadaddr ? inet_ntoa(((struct sockaddr_in *)ifaddr->ifa_broadaddr)->sin_addr) : "?");
        }

	freeifaddrs(ifaddrs);

        return EXIT_SUCCESS;
}
