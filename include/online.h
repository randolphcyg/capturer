#include <cJSON.h>
#include <stdio.h>

#include <lib/eal/include/rte_eal.h>
#include <lib/eal/include/rte_common.h>
#include <lib/log/rte_log.h>
#include <lib/ethdev/rte_ethdev.h>
#include <lib/mbuf/rte_mbuf.h>

void set_time_window(int seconds);

// Capture and dissect packet in real time
char *handle_packet(char *device_name, char *bpf_expr, int num, int promisc,
                    int to_ms);

// Set up callback function for send packet to wrap layer
typedef void (*DataCallback)(const char *, int, const char *, const char *);
void GetDataCallback(char *data, int length, char *device_name, char *windowKey);
void setDataCallback(DataCallback callback);