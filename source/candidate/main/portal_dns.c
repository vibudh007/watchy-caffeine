#include "portal_dns.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

enum { DNS_HEADER = 12, DNS_QUESTION = 4, DNS_ANSWER = 16, DNS_LIMIT = 512 };

static int dns_socket = -1;
static struct in_addr local_ip;

size_t portal_dns_reply(uint8_t *packet, size_t size, size_t capacity, const uint8_t address[4])
{
    if (size < DNS_HEADER || size > capacity ||
        (packet[2] & 0xfe) || packet[4] != 0 || packet[5] != 1 ||
        packet[6] || packet[7] || packet[8] || packet[9]) {
        return 0;
    }
    size_t end = DNS_HEADER;
    while (end < size && packet[end] != 0) {
        uint8_t length = packet[end++];
        if (length > 63 || length > size - end) {
            return 0;
        }
        end += length;
    }
    if (size - end < 1 + DNS_QUESTION || end - DNS_HEADER >= 255) {
        return 0;
    }
    end++;
    bool ipv4 = packet[end] == 0 && packet[end + 1] == 1 &&
                packet[end + 2] == 0 && packet[end + 3] == 1;
    end += DNS_QUESTION;
    if (ipv4 && capacity - end < DNS_ANSWER) {
        return 0;
    }

    packet[2] |= 0x84; /* Response, authoritative, preserve recursion desired. */
    packet[3] = 0;
    memset(packet + 6, 0, DNS_HEADER - 6);
    if (ipv4) {
        static const uint8_t record[] = {0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, 0, 0, 4};
        packet[7] = 1;
        memcpy(packet + end, record, sizeof record);
        memcpy(packet + end + sizeof record, address, 4);
        end += DNS_ANSWER;
    }
    return end;
}

bool portal_dns_start(const char *address)
{
    struct sockaddr_in local = {.sin_family = AF_INET, .sin_port = htons(53)};
    if (inet_pton(AF_INET, address, &local.sin_addr) != 1) {
        return false;
    }
    dns_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (dns_socket < 0 || fcntl(dns_socket, F_SETFL, O_NONBLOCK) < 0 ||
        bind(dns_socket, (const struct sockaddr *)&local, sizeof local) < 0) {
        portal_dns_stop();
        return false;
    }
    local_ip = local.sin_addr;
    return true;
}

void portal_dns_poll(void)
{
    if (dns_socket < 0) {
        return;
    }
    uint8_t packet[DNS_LIMIT + 1];
    for (unsigned i = 0; i < 4; i++) {
        struct sockaddr_in peer;
        socklen_t length = sizeof peer;
        ssize_t got = recvfrom(dns_socket, packet, sizeof packet, 0, (struct sockaddr *)&peer, &length);
        if (got <= 0) {
            return;
        }
        if (got <= DNS_LIMIT) {
            size_t reply = portal_dns_reply(packet, (size_t)got, DNS_LIMIT, (const uint8_t *)&local_ip.s_addr);
            if (reply) {
                sendto(dns_socket, packet, reply, 0, (const struct sockaddr *)&peer, length);
            }
        }
    }
}

void portal_dns_stop(void)
{
    if (dns_socket >= 0) {
        close(dns_socket);
        dns_socket = -1;
    }
}
