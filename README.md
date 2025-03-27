# capturer服务

## 服务说明
```shell
c的dpdk抓包,使用cgo回调,在go中将消息封装发到kafka，与pkt_parer服务成对使用。
消息生产改在c中也ok的，这里为了省事在go中实现了。
```

## docker部署
```shell
docker pull golang:1.24.1 --platform linux/amd64
sudo docker save golang:1.24.1  | gzip > golang-1.24.1.tar.gz
sudo docker load -i golang-1.24.1.tar.gz

docker pull ubuntu:22.04 --platform linux/amd64
sudo docker save ubuntu:22.04  | gzip > ubuntu-22.04.tar.gz
sudo docker load -i ubuntu-22.04.tar.gz

# 构建
sudo docker build --platform linux/amd64 -t capturer:1.0 .
# 容器导出
sudo docker save capturer:1.0  | gzip > capturer_1_0.tar.gz
# 解压镜像
docker load -i capturer_1_0.tar.gz

# 运行
docker run -d --privileged \
    --name capturer \
    --cap-add=NET_ADMIN \
    --cap-add=SYS_ADMIN \
    --device=/dev/vfio \
    -v /dev/hugepages:/dev/hugepages \
    -e DPDK_HUGEPAGES=1G \
    capturer:1.0 \
    -i "ens77" -pci "0000:02:05.0" -kafka "10.10.10.187:9092"
```

## docker镜像
CGO + DPDK + Kafka 容器
```shell
1. 确保宿主机支持DPDK
开启 Hugepages

echo 1024 > /proc/sys/vm/nr_hugepages
echo 2048 > /proc/sys/vm/nr_overcommit_hugepages
mkdir -p /dev/hugepages
mount -t hugetlbfs nodev /dev/hugepages

使用 1G Hugepages（推荐高性能场景）：
echo 8 > /sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages
mount -t hugetlbfs nodev /dev/hugepages

确保 Hugepages 永久生效，可以添加到 /etc/fstab：
nodev /dev/hugepages hugetlbfs defaults 0 0

2.绑定 DPDK 驱动
modprobe vfio-pci
echo "vfio-pci" > /sys/bus/pci/devices/0000:02:05.0/driver_override
echo 0000:02:05.0 > /sys/bus/pci/drivers/vfio-pci/bind

确定有哪些网卡
lspci | grep Ethernet
TODO: 适时增加网卡

dpdk绑定情况
dpdk-devbind.py --status
```

## docker-compose部署
```shell
docker-compose up -d
docker-compose down
```