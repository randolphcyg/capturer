#include <cJSON.h>
#include <lib/eal/include/rte_common.h>
#include <lib/eal/include/rte_eal.h>
#include <lib/ethdev/rte_ethdev.h>
#include <lib/log/rte_log.h>
#include <lib/mbuf/rte_mbuf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <uthash.h>

// Capture and dissect packet in real time
char *handle_packet(char *device_name, const char *pci_addr);

// Set up callback function for send packet to wrap layer
typedef void (*DataCallback)(const char *, int, const char *);
void GetDataCallback(char *data, int length, char *windowKey);
void setDataCallback(DataCallback callback);