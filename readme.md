当存在一个 storage 已经连接到 master 时，再开启一个 storage 注册到 master 时，storage 的 master_conn 会收到 master 没有发送过的消息，即使抓包也不存在该消息。

![alt text](QQ_1737806153037.png)
