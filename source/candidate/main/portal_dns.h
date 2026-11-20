#ifndef WATCHY_PORTAL_DNS_H
#define WATCHY_PORTAL_DNS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

size_t portal_dns_reply(uint8_t *packet, size_t size, size_t capacity, const uint8_t address[4]);
bool portal_dns_start(const char *address);
void portal_dns_poll(void);
void portal_dns_stop(void);

#endif
