
基于RESP协议，主从复制逻辑技术方案可行

#编译
主节点kvstore
make
子节点kvstore_slave
make server_slave
单独增量存储测试用例
make testcase_add
单独全量存储测试用例
make testcase_full
增量全量存储测试用例
make testcase_save
slave的同步信息测试用例
make testcase_slave

#运行主节点
./kvstore      
#运行子节点
./kvstore_slave     

redis-benchmark测试的QPS平均值为400


