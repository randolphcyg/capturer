package capturer

/*
#cgo pkg-config: glib-2.0
#cgo CFLAGS: -I${SRCDIR}/include
#cgo CFLAGS: -I${SRCDIR}/include/dpdk -msse4.1
#cgo CFLAGS: -I${SRCDIR}/include/dpdk -I${SRCDIR}/include/dpdk/config
#cgo CFLAGS: -I${SRCDIR}/include/dpdk/lib/eal/include
#cgo LDFLAGS: -L${SRCDIR}/lib -lrte_mempool -lrte_log -lrte_eal -lrte_ethdev -lrte_mbuf

#include "online.h"
*/
import "C"
import (
	"log/slog"
	"unsafe"

	"github.com/confluentinc/confluent-kafka-go/kafka"
	"github.com/pkg/errors"
)

var P *kafka.Producer
var kafkaChan chan *kafka.Message

// InitKafkaProducer 初始化 Kafka 生产者
func InitKafkaProducer(addr string) error {
	config := &kafka.ConfigMap{
		"bootstrap.servers":        addr,
		"message.send.max.retries": 3,
		"compression.codec":        "snappy",
	}

	var err error
	P, err = kafka.NewProducer(config)
	if err != nil {
		return err
	}

	kafkaChan = make(chan *kafka.Message, 10000) // 缓冲队列，减少阻塞
	go kafkaWorker()                             // 启动 Kafka 生产者
	slog.Info("Kafka producer init successfully")
	return nil
}

func kafkaWorker() {
	for msg := range kafkaChan {
		if err := P.Produce(msg, nil); err != nil {
			slog.Error("Failed to send message to Kafka", "err", err)
		}
	}
}

// SendToKafka 生产者 将dpdk抓到的包存储到kafka
func SendToKafka(topic string, windowKey string, packet []byte) {
	slog.Info("",
		"windowKey", windowKey,
		"len", len(packet))

	msg := &kafka.Message{
		TopicPartition: kafka.TopicPartition{Topic: &topic, Partition: kafka.PartitionAny},
		Key:            []byte(windowKey), // 使用时间窗口作为 Key
		Value:          packet,            // 直接存储二进制数据
	}

	select {
	case kafkaChan <- msg: // 非阻塞写入通道
	default:
		slog.Warn("Kafka buffer is full, dropping packet")
	}
}

//export GetDataCallback
func GetDataCallback(data *C.char, length C.int, interfaceName *C.char, windowKey *C.char) {
	if data == nil || length <= 0 {
		slog.Info("Received empty packet data")
		return
	}

	// 将 C 传来的数据转换为 Go 的 []byte
	goPacket := unsafe.Slice((*byte)(unsafe.Pointer(data)), int(length))

	interfaceNameStr := C.GoString(interfaceName)
	windowKeyStr := C.GoString(windowKey)

	// 发送到 Kafka
	SendToKafka(interfaceNameStr, windowKeyStr, goPacket)
}

func StartLivePacketCapture(interfaceName string, pciAddr string) (err error) {
	// 回调函数
	C.setDataCallback((C.DataCallback)(C.GetDataCallback))

	if interfaceName == "" {
		err = errors.Wrap(err, "device name is blank")
		return
	}

	errMsg := C.handle_packet(C.CString(interfaceName), C.CString(pciAddr))
	if C.strlen(errMsg) != 0 {
		err = errors.Errorf("fail to capture packet live:%s", C.GoString(errMsg))
		return
	}

	return nil
}
