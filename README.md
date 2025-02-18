
```shell
docker save -o golang-1.24.0.tar golang:1.24.0
docker load -i golang-1.24.0.tar

sudo docker build --platform linux/amd64 -t capturer:1.0 .

docker pull golang:1.24.0 --platform linux/amd64

# 构建
sudo docker build --platform linux/amd64 -t capturer:1.0 .

sudo docker build -t capturer:1.0 .

# 容器导出
sudo docker save capturer:1.0  | gzip > capturer_1_0.tar.gz
# 解压镜像
docker load -i capturer_1_0.tar.gz

# 运行
docker run --rm -it --privileged \
    --cap-add=NET_ADMIN \
    --cap-add=SYS_ADMIN \
    --device=/dev/vfio \
    -v /dev/hugepages:/dev/hugepages \
    -e DPDK_HUGEPAGES=1G \
    capturer:1.0 -- \
    -i "ens66" -pic "0000:02:05.0" -kafka "192.168.3.93:9092"
```