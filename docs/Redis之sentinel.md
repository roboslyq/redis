# Redis之Sentinel

# 基本概念

### 下线定义
- 主观下线（sdown）

  - 具体的某个sentinel实例本身对服务实例的判断，当服务实例不可到达时，认为服务实例已经下线。

- b.客观下线（odown）：

  - 多个sentinel实例对同一个服务SDOWN的状态做出协商后的判断，只有master才可能在odown状态
    简单的说，一个sentinel单独做出的判断只能是sdown，是没有任何官方效力的，只有多个sentinel大家商量好，得到一致，才能将某个master状态置为odown，只有确定master odown状态后，才能做后续fail over的操作。

    > 此处判断下线，是分布式中一 个最常见的场景，即投票。一致性协议Raft和Pasox均会使用这种思想。

### 通信
- sentinel与maste/slave的交互主要包括：
  a.PING:sentinel向其发送PING以了解其状态（是否下线）
  b.INFO:sentinel向其发送INFO以获取replication相关的信息
  c.PUBLISH:sentinel向其监控的master/slave发布本身的信息及master相关的配置
  d.SUBSCRIBE:sentinel通过订阅master/slave的”__sentinel__:hello“频道以获取其它正在监控相同服务的sentinel
- sentinel与sentinel的交互主要包括：
  a.PING:sentinel向slave发送PING以了解其状态（是否下线）
  b.SENTINEL is-master-down-by-addr：和其他sentinel协商master状态，如果master odown，则投票选出leader做fail over

### Fail over
一次完整的fail over包括以下步骤：
a. sentinel发现master下线，则标记master sdown
b. 和其他sentinel协商以确定master状态是否odown
c. 如果master odown，则选出leader
d. 当选为leader的sentinel选出一个slave做为master，并向该slave发送slaveof no one命令以转变slave角色为master
e. 向已下线的master及其他slave发送**slaveof xxx**命令使其作为新当选master的slave



# 启动流程

启动Sentinel程序，有两种命令方式：

```sh
./redis-sentinel sentinel.conf
或者
./redis-server --sentinel sentinel.con
```



其初始化流程和redis-server执行一致，程序初始化会判断是否处于sentinel模式，如果处于sentinel模式，则会完成一些sentinel相关的初始化工作，主要包括：
1）读取sentinel相关配置
2）初始化sentinel状态，并添加master、sentinel及slave到相应的字典
3）注册sentinel相关的time event（周期性执行）

redis-sentinel和redis-server是同一个程序，查看Makefile文件可知：
$(REDIS_SENTINEL_NAME): $(REDIS_SERVER_NAME)
$(REDIS_INSTALL) $(REDIS_SERVER_NAME) $(REDIS_SENTINEL_NAME) 

# 参考资料

[Redis Sentinel源码分析（一）](https://blog.csdn.net/yfkiss/article/details/22151175)

[redis系列之sentinel结构的网络构建](https://www.jianshu.com/p/95e7b3cab6d5)

