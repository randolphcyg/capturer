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
	"log"
	"unsafe"

	"github.com/confluentinc/confluent-kafka-go/kafka"
	"github.com/pkg/errors"
)

var P *kafka.Producer

func init() {
	var err error
	P, err = kafka.NewProducer(&kafka.ConfigMap{"bootstrap.servers": "192.168.3.93:9092"})
	if err != nil {
		panic(err)
		return
	}

}

// CChar2GoStr C string -> Go string
func CChar2GoStr(src *C.char) string {
	return C.GoStringN(src, C.int(C.strlen(src)))
}

// SetTimeWindow 设置时间窗口
func SetTimeWindow(seconds int) {
	C.set_time_window(C.int(seconds))
}

func produceToKafka(device string, windowKey string, packet []byte) {
	topic := device
	msg := &kafka.Message{
		TopicPartition: kafka.TopicPartition{Topic: &topic, Partition: kafka.PartitionAny},
		Key:            []byte(windowKey), // 使用时间窗口作为 Key
		Value:          packet,            // 直接存储二进制数据
	}

	if err := P.Produce(msg, nil); err != nil {
		log.Printf("Failed to send message: %s", err)
	}
}

//export GetDataCallback
func GetDataCallback(data *C.char, length C.int, interfaceName *C.char, windowKey *C.char) {
	if data == nil || length <= 0 {
		log.Println("Received empty packet data")
		return
	}

	// 将 C 传来的数据转换为 Go 的 []byte
	goPacket := C.GoBytes(unsafe.Pointer(data), length)

	interfaceNameStr := ""
	if interfaceName != nil {
		interfaceNameStr = C.GoString(interfaceName)
	}

	windowKeyStr := ""
	if windowKey != nil {
		windowKeyStr = C.GoString(windowKey)
	}

	// 发送到 Kafka
	produceToKafka(interfaceNameStr, windowKeyStr, goPacket)
}

func StartLivePacketCapture(interfaceName, bpfFilter string, packetCount, promisc, timeout int) (err error) {
	// 回调函数
	C.setDataCallback((C.DataCallback)(C.GetDataCallback))

	if interfaceName == "" {
		err = errors.Wrap(err, "device name is blank")
		return
	}

	errMsg := C.handle_packet(C.CString(interfaceName), C.CString(bpfFilter), C.int(packetCount),
		C.int(promisc), C.int(timeout))
	if C.strlen(errMsg) != 0 {
		// transfer c char to go string
		errMsgStr := CChar2GoStr(errMsg)
		err = errors.Errorf("fail to capture packet live:%s", errMsgStr)
		return
	}

	return
}
