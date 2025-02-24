package capturer

/*
#cgo pkg-config: glib-2.0
#cgo CFLAGS: -I${SRCDIR}/include
#cgo CFLAGS: -I${SRCDIR}/include/dpdk -msse4.1
#cgo CFLAGS: -I${SRCDIR}/include/dpdk -I${SRCDIR}/include/dpdk/config
#cgo CFLAGS: -I${SRCDIR}/include/dpdk/lib/eal/include
#cgo LDFLAGS: -L${SRCDIR}/lib -lrte_mempool -lrte_log -lrte_eal -lrte_ethdev -lrte_mbuf

#include "capture.h"
*/
import "C"
import (
	"context"
	"log/slog"
	"time"
	"unsafe"

	"github.com/pkg/errors"
	"github.com/segmentio/kafka-go"
)

var (
	writer    *kafka.Writer
	kafkaChan chan kafka.Message
)

// InitKafkaProducer 初始化 Kafka 生产者
func InitKafkaProducer(broker, topic string, kafkaBatchSize, bufferSize int) error {
	if broker == "" {
		return errors.New("kafka broker address is empty")
	}

	// 配置 Kafka Writer
	writer = &kafka.Writer{
		Addr:         kafka.TCP(broker),   // Kafka 地址
		Balancer:     &kafka.LeastBytes{}, // 分区负载均衡策略
		Topic:        topic,
		RequiredAcks: kafka.RequireAll,       // 需要所有副本确认
		MaxAttempts:  3,                      // 最大重试次数
		Compression:  kafka.Snappy,           // 压缩算法
		BatchSize:    kafkaBatchSize,         // 批量发送大小
		BatchTimeout: 100 * time.Millisecond, // 批量发送超时时间
		Async:        true,                   // 异步发送
	}

	kafkaChan = make(chan kafka.Message, bufferSize) // 缓冲队列，减少阻塞
	go kafkaWorker()                                 // 启动 Kafka 生产者
	slog.Info("Kafka producer init successfully")
	return nil
}

// kafkaWorker 从缓冲队列中读取消息并发送到 Kafka
func kafkaWorker() {
	for msg := range kafkaChan {
		if err := writer.WriteMessages(context.Background(), msg); err != nil {
			slog.Error("Failed to send message to Kafka", "err", err)
		}
	}
}

// sendToKafka 生产者 将dpdk抓到的包存储到kafka
func sendToKafka(key string, value []byte) error {
	if writer == nil {
		return errors.New("kafka producer is not initialized")
	}

	msg := kafka.Message{
		Key:   []byte(key), // 使用时间窗口作为 Key
		Value: value,       // 直接存储二进制数据
	}

	select {
	case kafkaChan <- msg: // 将消息放入缓冲队列
	default:
		slog.Warn("Kafka buffer is full, dropping packet")
	}

	return nil
}

// CloseKafkaProducer 关闭 Kafka 生产者
func CloseKafkaProducer() {
	close(kafkaChan) // 关闭通道
	if writer != nil {
		if err := writer.Close(); err != nil {
			slog.Error("Failed to close Kafka writer", "err", err)
		}
	}
	slog.Info("Kafka producer closed")
}

//export GetDataCallback
func GetDataCallback(data *C.char, length C.int, windowKey *C.char) {
	if data == nil || length <= 0 {
		slog.Info("Received empty packet data")
		return
	}

	// 将 C 传来的数据转换为 Go 的 []byte
	goPacket := unsafe.Slice((*byte)(unsafe.Pointer(data)), int(length))

	windowKeyStr := C.GoString(windowKey)

	slog.Info("",
		"windowKey", windowKeyStr,
		"len", int(length))

	// 发送到 Kafka
	if err := sendToKafka(windowKeyStr, goPacket); err != nil {
		slog.Warn("Error: sendToKafka", "error", err)
	}
}

func StartLivePacketCapture(ifName string, pciAddr string) (err error) {
	// 回调函数
	C.setDataCallback((C.DataCallback)(C.GetDataCallback))

	if ifName == "" {
		err = errors.Wrap(err, "device name is blank")
		return
	}

	errMsg := C.handle_packet(C.CString(ifName), C.CString(pciAddr))
	if C.strlen(errMsg) != 0 {
		err = errors.Errorf("fail to capture packet live:%v", C.GoString(errMsg))
		return
	}

	return nil
}
