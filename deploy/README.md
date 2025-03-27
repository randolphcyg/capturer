
当前测试俩节点：10.10.10.187 10.10.10.190
若增加kafka节点，则只需要修改每个节点的KAFKA_CONTROLLER_QUORUM_VOTERS参数，增加新节点信息；
新节点参考node2的配置文件，指启动kafka即可，由于收费原因，弃用cp-kafka镜像。

kafka和web的镜像
```shell
docker pull bitnami/kafka:latest --platform linux/amd64
# 修改ubuntu22上镜像tag
sudo docker tag bitnami/kafka:latest bitnami/kafka:latest-u22
# 将原先tag变成 <none> 的在命名为bitnami/kafka:latest

sudo docker save bitnami/kafka:latest-u22  | gzip > kafka.tar.gz
sudo docker load -i kafka.tar.gz

docker pull provectuslabs/kafka-ui:master --platform linux/amd64
# 修改ubuntu22上镜像tag
sudo docker tag provectuslabs/kafka-ui:master provectuslabs/kafka-ui:master-u22
# 将原先tag变成 <none> 的在命名为 provectuslabs/kafka-ui:master

sudo docker save provectuslabs/kafka-ui:master-u22  | gzip > kafka-ui.tar.gz
sudo docker load -i kafka-ui.tar.gz
```

生产上kafka更新了ui没更新

docker-compose部署
```shell
sudo mkdir -p /var/lib/kafka/data
sudo chmod -R 777 /var/lib/kafka/data


kafka部署需要kafka和web的镜像
然后 KAFKA_ADVERTISED_LISTENERS 参数的ip需要修改
然后用下面命令启停

docker-compose up -d
docker-compose down


capturer 服务需要dpdk专属网卡， 抓到的包存到自定义指定topic下 例如 ens33
kafka topic key是毫秒级时间戳，保证消息顺序性


pkt_parser 会启动6个副本 这个数量和kafka的topic分片区数目一致 小于等于 kafka分片区数目，kafka会自动负载均衡
会消费capturer存储的消息，通过libwireshark解析后将协议树json存到kafka的 指定 topic 例如 ens33_parsed_pkts 下

```


kafka设置
```shell
已经启动了 Kafka 集群，可以使用以下命令为现有的主题添加配置：

# 修改主题配置，设置保留时间为 14 天（14 * 24 * 60 * 60 * 1000 毫秒）和保留大小为 10GB（10 * 1024 * 1024 * 1024 字节）
kafka-configs --bootstrap-server 192.168.3.93:9092 --entity-type topics --entity-name your_topic_name --alter --add-config retention.ms=1209600000,retention.bytes=10737418240

新主题
# 创建新主题并设置保留时间和保留大小
kafka-topics --bootstrap-server 192.168.3.93:9092 --create --topic your_topic_name --partitions 6 --replication-factor 1 --config retention.ms=1209600000 --config retention.bytes=10737418240

```
