package main

import (
	"context"
	"flag"
	"log/slog"
	"os"
	"os/signal"
	"syscall"

	"capturer"
)

var ifName = flag.String("i", "ens33", "interface name")
var pciAddr = flag.String("pci", "0000:02:01.0", "pci addr")
var kafkaAddr = flag.String("kafka", "10.10.10.187:9092", "kafka address")
var kafkaBatchSize = flag.Int("batchSize", 1000, "kafka 批量发送大小")
var bufferSize = flag.Int("bufferSize", 10000, "缓冲队列大小")

// waitForShutdown 等待退出信号
func waitForShutdown(ctx context.Context) {
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)

	select {
	case <-sigChan:
		slog.Info("Received shutdown signal, shutting down...")
	case <-ctx.Done():
		slog.Info("Context canceled, shutting down...")
	}
}

func main() {
	flag.Parse()

	// 创建上下文，用于优雅退出
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	// 设置 Kafka 地址并初始化 Kafka 生产者
	topic := *ifName
	if err := capturer.InitKafkaProducer(*kafkaAddr, topic, *kafkaBatchSize, *bufferSize); err != nil {
		slog.Error("Failed to initialize Kafka producer", "info", err)
		return
	}

	// 关闭 Kafka 生产者
	defer capturer.CloseKafkaProducer()

	// 启动抓包
	err := capturer.StartLivePacketCapture(*ifName, *pciAddr)
	if err != nil {
		slog.Error("Failed to start packet capture", "info", err)
	}

	// 等待退出信号
	waitForShutdown(ctx)

	slog.Info("Packet capture stopped, exiting...")
}
