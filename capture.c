#include <capture.h>

#define ERRBUF_SIZE 256

static bool process_packet(char *device_name, const struct rte_mbuf *mbuf,
                           const char *window_key);

// Set up callback function for send packet to Go
static DataCallback dataCallback;
void setDataCallback(DataCallback callback) { dataCallback = callback; }

// 时间戳 Key 毫秒级精度
char *get_window_key() {
    static char key[32];
    struct timeval tv;
    gettimeofday(&tv, NULL);
    snprintf(key, sizeof(key), "%ld%03ld", tv.tv_sec, tv.tv_usec / 1000);
    return key;
}

/**
 * 初始化 DPDK 环境。
 *
 * @param argc 参数数量。
 * @param argv 参数数组。
 * @return 成功返回 0，失败返回 -1。
 */
int init_dpdk(int argc, char **argv, const char *pci_addr) {
  // 添加默认的 EAL 参数
  const char *default_eal_args[] = {
      "capturer",           // 程序名称
      "-l",       "0-3",    // 指定核心
      "-n",       "4",      // 内存通道
      "-a",       pci_addr, // 使用传入的 PCI 地址
  };

  // 合并用户参数和默认参数
  int total_args =
      argc + sizeof(default_eal_args) / sizeof(default_eal_args[0]);
  char *eal_args[total_args];

  // 复制默认参数
  for (int i = 0; i < sizeof(default_eal_args) / sizeof(default_eal_args[0]);
       i++) {
    eal_args[i] = (char *)default_eal_args[i];
  }

  // 复制用户参数
  for (int i = 0; i < argc; i++) {
    eal_args[sizeof(default_eal_args) / sizeof(default_eal_args[0]) + i] =
        argv[i];
  }

  // 初始化 DPDK 环境
  RTE_LOG(INFO, USER1, "Init DPDK with EAL arguments:\n");
  for (int i = 0; i < total_args; i++) {
    RTE_LOG(INFO, USER1, "  %s\n", eal_args[i]);
  }

  int ret = rte_eal_init(total_args, eal_args);
  if (ret < 0) {
    RTE_LOG(ERR, USER1, "Failed to init DPDK: %s\n", rte_strerror(-ret));
    return -1;
  }

  RTE_LOG(INFO, USER1, "DPDK init successfully\n");
  return 0;
}

int port_init(uint16_t port, struct rte_mempool *mbuf_pool) {
  struct rte_eth_conf port_conf = {
      .rxmode =
          {
              .mtu = RTE_ETHER_MAX_LEN, // 或者使用 max_lro_pkt_size
              .mq_mode = RTE_ETH_MQ_RX_NONE,
          },
      .intr_conf = {
          .lsc = 0, // 禁用链路状态变化中断
          .rxq = 0, // 禁用接收队列中断
      }};
  int ret = rte_eth_dev_configure(port, 1, 1, &port_conf);
  if (ret != 0) {
    return ret;
  }

  ret = rte_eth_rx_queue_setup(port, 0, 128, rte_eth_dev_socket_id(port), NULL,
                               mbuf_pool);
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

// 处理每个包
static bool process_packet(char *device_name, const struct rte_mbuf *mbuf,
                           const char *window_key) {
  // 从 rte_mbuf 中提取数据包内容
  const u_char *packet = rte_pktmbuf_mtod(mbuf, const u_char *);
  if (!packet) {
    RTE_LOG(ERR, USER1, "Failed to extract packet data from mbuf\n");
    return false;
  }

  // 直接调用回调，将二进制数据传到 Go
  if (dataCallback != NULL) {
    dataCallback((char *)packet, mbuf->data_len, device_name, window_key);
  }

  return true;
}

// dpdk抓包循环逻辑
void dpdk_capture_loop(char *device_name) {
  struct rte_mbuf *mbufs[32]; // 接收缓冲区
  uint16_t nb_rx;             // 接收到的数据包数量
  uint16_t port = 0;          // 使用单个端口

  RTE_LOG(INFO, USER1, "Starting DPDK capture loop on port %u\n", port);

  while (1) {
    // 从端口接收数据包
    nb_rx = rte_eth_rx_burst(port, 0, mbufs, 32); // 一次最多接收 32 个数据包
    if (nb_rx == 0) {
      RTE_LOG(DEBUG, USER1, "No packets received\n");
      continue;
    }

    RTE_LOG(DEBUG, USER1, "Received %u packets\n", nb_rx);

    // 获取当前时间窗口的 Key
    char *window_key = get_window_key();

    // 处理每个数据包
    for (int i = 0; i < nb_rx; i++) {
      struct rte_mbuf *mbuf = mbufs[i];

      // 处理数据包
      if (!process_packet(device_name, mbuf, window_key)) {
        RTE_LOG(ERR, USER1, "Failed to process packet %d\n", i);
      }

      // 释放 mbuf
      rte_pktmbuf_free(mbuf);
    }
  }
}

char *handle_packet(char *device_name, const char *pci_addr) {
  printf("Init DPDK!\n");

  int argc = 0;
  char *argv[] = {"capturer"}; // 可以添加更多参数
  if (init_dpdk(argc, argv, pci_addr) < 0) {
    return "Failed to initialize DPDK";
  }

  // Create memory pool
  struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create(
      "MBUF_POOL", 8192, 512, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
  if (mbuf_pool == NULL) {
    return "Cannot create mbuf pool";
  }

  // Initialize port
  if (port_init(0, mbuf_pool) != 0) {
    return "Cannot init DPDK port";
  }

  printf("Start DPDK capture!\n");

  // Start capture loop
  dpdk_capture_loop(device_name);

  return "";
}