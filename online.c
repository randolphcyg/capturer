#include <online.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <uthash.h>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

#include <arpa/inet.h>  // 需要这个头文件
#include <stdio.h>      // 需要 sprintf

#define ERRBUF_SIZE 256

// 时间窗口配置（单位：秒）
static int time_window_seconds = 1; // 默认 1 秒

// 设置时间窗口
void set_time_window(int seconds) {
    if (seconds > 0) {
        time_window_seconds = seconds;
    }
}

// 获取当前时间窗口的 Key
char *get_window_key() {
    static char key[32];
    time_t now = time(NULL);
    time_t window_start = (now / time_window_seconds) * time_window_seconds;
    snprintf(key, sizeof(key), "%ld", window_start);
    return key;
}

// Base64 编码函数
char *base64_encode(const unsigned char *input, int length) {
    BIO *bio, *b64;
    BUF_MEM *bufferPtr;

    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);

    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);  // 不添加换行符
    BIO_write(bio, input, length);
    BIO_flush(bio);
    BIO_get_mem_ptr(bio, &bufferPtr);

    char *output = (char *)malloc(bufferPtr->length + 1);
    memcpy(output, bufferPtr->data, bufferPtr->length);
    output[bufferPtr->length] = '\0';

    BIO_free_all(bio);

    return output;
}

// device_content Contains the information needed for each device
typedef struct device_content {
  char *device;
  char *bpf_expr;
  int num;
  int promisc;
  int to_ms;

  u_char *packet;
  int packet_len;
} device_content;

struct device_map {
  char *device_name;
  device_content content;
  UT_hash_handle hh;
};

// global map to restore device info
struct device_map *devices = NULL;

char *add_device(char *device_name, char *bpf_expr, int num, int promisc,
                 int to_ms);
struct device_map *find_device(char *device_name);

static bool send_data_to_wrap(struct device_map *device, const char *window_key);
static bool process_packet(struct device_map *device, const struct rte_mbuf *mbuf, const char *window_key);

char *stop_dissect_capture_pkg(char *device_name);

// Set up callback function for send packet to Go
static DataCallback dataCallback;
void setDataCallback(DataCallback callback) { dataCallback = callback; }


char *add_device(char *device_name, char *bpf_expr, int num, int promisc, int to_ms) {
  char *err_msg;
  struct device_map *s;

  HASH_FIND_STR(devices, device_name, s);
  if (s == NULL) {
    s = (struct device_map *)malloc(sizeof *s);
    memset(s, 0, sizeof(struct device_map));

    s->device_name = device_name;
    s->content.bpf_expr = bpf_expr;
    s->content.num = num;
    s->content.promisc = promisc;
    s->content.to_ms = to_ms;

    HASH_ADD_KEYPTR(hh, devices, s->device_name, strlen(s->device_name), s);
    return "";
  } else {
    return "The device is in use";
  }
}

struct device_map *find_device(char *device_name) {
  struct device_map *s;

  HASH_FIND_STR(devices, device_name, s);
  return s;
}


/**
 * 初始化 DPDK 环境。
 *
 * @param argc 参数数量。
 * @param argv 参数数组。
 * @return 成功返回 0，失败返回 -1。
 */
int init_dpdk(int argc, char **argv) {
    // 添加默认的 EAL 参数
    const char *default_eal_args[] = {
        "capturer", // 程序名称
        "-l", "0-3",    // 指定核心
        "-n", "4",      // 内存通道
    };

    // 合并用户参数和默认参数
    int total_args = argc + sizeof(default_eal_args) / sizeof(default_eal_args[0]);
    char *eal_args[total_args];

    // 复制默认参数
    for (int i = 0; i < sizeof(default_eal_args) / sizeof(default_eal_args[0]); i++) {
        eal_args[i] = (char *)default_eal_args[i];
    }

    // 复制用户参数
    for (int i = 0; i < argc; i++) {
        eal_args[sizeof(default_eal_args) / sizeof(default_eal_args[0]) + i] = argv[i];
    }

    // 初始化 DPDK 环境
    RTE_LOG(INFO, USER1, "Initializing DPDK with EAL arguments:\n");
    for (int i = 0; i < total_args; i++) {
        RTE_LOG(INFO, USER1, "  %s\n", eal_args[i]);
    }

    int ret = rte_eal_init(total_args, eal_args);
    if (ret < 0) {
        RTE_LOG(ERR, USER1, "Failed to initialize DPDK: %s\n", rte_strerror(-ret));
        return -1;
    }

    RTE_LOG(INFO, USER1, "DPDK initialized successfully\n");
    return 0;
}

int port_init(uint16_t port, struct rte_mempool *mbuf_pool) {
    struct rte_eth_conf port_conf = {
        .rxmode = {
            .mtu = RTE_ETHER_MAX_LEN, // 或者使用 max_lro_pkt_size
            .mq_mode = RTE_ETH_MQ_RX_NONE,
        },
        .intr_conf = {
           .lsc = 0, // 禁用链路状态变化中断
           .rxq = 0, // 禁用接收队列中断
        }
    };
    int ret = rte_eth_dev_configure(port, 1, 1, &port_conf);
    if (ret != 0) {
        return ret;
    }

    ret = rte_eth_rx_queue_setup(port, 0, 128, rte_eth_dev_socket_id(port), NULL, mbuf_pool);
    if (ret < 0) {
        return ret;
    }

    ret = rte_eth_tx_queue_setup(port, 0, 512, rte_eth_dev_socket_id(port), NULL);
    if (ret < 0) {
        return ret;
    }

    ret = rte_eth_dev_start(port);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

// 将dpdk抓取的流量发到go 存到kafka
static bool send_data_to_wrap(struct device_map *device, const char *window_key) {
  bool success = false;

  // 检查 device 和 packet 是否有效
  if (!device || !device->content.packet) {
      RTE_LOG(ERR, USER1, "Device or packet data is NULL\n");
      return false;
  }

  // 发送数据到 Go 回调函数
  if (dataCallback != NULL) {
    int len = strlen(device->content.packet);
    dataCallback(device->content.packet, len, device->device_name, window_key);
    success = true;
  }

  return success;
}

// 处理每个包
static bool process_packet(struct device_map *device, const struct rte_mbuf *mbuf, const char *window_key) {
  // 检查设备是否有效
  if (!device) {
      RTE_LOG(ERR, USER1, "Device is NULL\n");
      return false;
  }

  // 从 rte_mbuf 中提取数据包内容
  const u_char *packet = rte_pktmbuf_mtod(mbuf, const u_char *);
  if (!packet) {
      RTE_LOG(ERR, USER1, "Failed to extract packet data from mbuf\n");
      return false;
  }

  // 将数据包内容和元信息打包为 JSON
  cJSON *root = cJSON_CreateObject();
  if (!root) {
      RTE_LOG(ERR, USER1, "Failed to create JSON object\n");
      return false;
  }

  // 添加数据包内容（Base64 编码，避免二进制数据问题）
  char *packet_base64 = base64_encode(packet, mbuf->data_len);
  if (!packet_base64) {
      RTE_LOG(ERR, USER1, "Failed to encode packet data to Base64\n");
      cJSON_Delete(root);
      return false;
  }
  cJSON_AddStringToObject(root, "packet_data", packet_base64);
  free(packet_base64);

  // 添加元信息
  cJSON_AddNumberToObject(root, "packet_len", mbuf->data_len);
  cJSON_AddStringToObject(root, "timestamp", window_key);

  // 将 JSON 对象转换为字符串
  char *json_str = cJSON_PrintUnformatted(root);
  if (!json_str) {
      RTE_LOG(ERR, USER1, "Failed to print JSON\n");
      cJSON_Delete(root);
      return false;
  }

  // 打印 JSON 数据（调试用）
  RTE_LOG(INFO, USER1, "JSON data: %s\n", json_str);

  // 将 JSON 数据存储到 device->content.packet
  if (device->content.packet) {
      free(device->content.packet);  // 释放之前的数据
  }
  device->content.packet = json_str;

  // 将时间窗口 Key 和数据包传递给 Go 回调函数
  if (!send_data_to_wrap(device, window_key)) {
     RTE_LOG(ERR, USER1, "Failed to send data to Kafka\n");
     cJSON_Delete(root);
//     free(json_str);
     return false;
  }

  // 释放资源
  cJSON_Delete(root);
//  free(json_str);

  return true;
}

// dpdk抓包循环逻辑
void dpdk_capture_loop(struct device_map *device) {
    struct rte_mbuf *mbufs[32]; // 接收缓冲区
    uint16_t nb_rx;             // 接收到的数据包数量
    uint16_t port = 0;          // 假设使用单个端口

    RTE_LOG(INFO, USER1, "Starting DPDK capture loop on port %u\n", port);

    while (1) {
        // 从端口接收数据包
        nb_rx = rte_eth_rx_burst(port, 0, mbufs, 32); // 一次最多接收 32 个数据包
        if (nb_rx == 0) {
            RTE_LOG(DEBUG, USER1, "No packets received\n");
            continue;
        }

        // RTE_LOG(INFO, USER1, "Received %u packets\n", nb_rx);

        // 获取当前时间窗口的 Key
        char *window_key = get_window_key();

        // 处理每个数据包
        for (int i = 0; i < nb_rx; i++) {
            struct rte_mbuf *mbuf = mbufs[i];

            // 处理数据包
            if (!process_packet(device, mbuf, window_key)) {
                RTE_LOG(ERR, USER1, "Failed to process packet %d\n", i);
            }

            // 释放 mbuf
            rte_pktmbuf_free(mbuf);
        }
    }
}

char *handle_packet(char *device_name, char *bpf_expr, int num, int promisc,
                    int to_ms) {
  char *err_msg;
  char err_buf[ERRBUF_SIZE];
  // add a device to global device map
  err_msg = add_device(device_name, bpf_expr, num, promisc, to_ms);
  if (err_msg != NULL) {
    if (strlen(err_msg) != 0) {
      return err_msg;
    }
  }

  // fetch target device
  struct device_map *device = find_device(device_name);
  if (!device) {
    return "The device is not in the global map";
  }

  printf("初始化 DPDK: %s \n", "!!!!!!!!!");
  int argc = 0;
  char *argv[] = { "capturer" }; // 可以添加更多参数
  if (init_dpdk(argc, argv) < 0) {
      return "Failed to initialize DPDK";
  }

  // Create memory pool
  struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", 8192,
                                                          512, 0, RTE_MBUF_DEFAULT_BUF_SIZE,
                                                          rte_socket_id());
  if (mbuf_pool == NULL) {
      return "Cannot create mbuf pool";
  }

  // Initialize port
  if (port_init(0, mbuf_pool) != 0) {
      return "Cannot init port";
  }

  printf("开始DPDK抓包: %s \n", "!!!!!!!!!");

  // Start capture loop
  dpdk_capture_loop(device);

  return "";
}