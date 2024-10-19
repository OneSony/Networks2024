# FTP Report
## 简介


## Server

### 基本结构
存在一个监听进程, 监听指定端口的TCP连接. 当有client连接至server的指定端口后, fork新进程用于处理该client的所有操作. 当该client结束后, 对应的进程也结束.  
在每个处理client的主进程中, 通过control socket与client联络. 主进程中维护一个消息循环, 阻塞监听control socket传来的client的指令. 根据每条指令执行不同的server操作, 并通过control socket给予回复. 对于RETR, STOR, LIST指令, 会在主进程外额外fork开辟数据传输进程DTP. Server确保主进程仅维护control socket, DTP仅维护data socket.
在处理需要DTP的指令时, 首先根据PORT或PASV建立与client的数据传输TCP连接. 当数据传输的TCP连接建立成功后, 在主进程中fork子进程专一维护数据传输相关的功能, 如文件读取, 文件发送, 文件写入等. 主进程在此时专一维护control socket. 在本次实现中, 主进程于DTP通过管道相连, 用于处理数据传输过程中control socket传来的ABOR指令.
本实验实现了数据传输中响应ABOR指令. 当数据传输中收到ABOR指令后, 主进程向DTP(子进程)发送SIGTERM消息. DTP中重写了SIGTERM的响应, 会将开启的socket接口, 文件接口, 管道接口等关闭后结束数据传输, 并结束DTP进程. 主进程会在此时通过control socket向client发送回复, 来完成ABOR的规范.

TODO 服务器记录状态!!!


### 实现功能
基本功能: USER, PASS, RETR, STOR, QUIT, SYST, TYPE, PORT, PASV, MKD, CWD, PWD, LIST, RMD.

额外功能: 
ABOR: 支持RETR, STOR, LIST指令传输中的中断.
SIZE: 可以返回指定文件大小. 搭配Linux内建的ftp客户端, 可以实现文件传输进度的显示.
REST: 设定文件传输指针位置. 结合SIZE指令或本地文件属性, 可以实现文件续传.
NLST: 提供NLST格式的文件列表. 搭配Linux内建的ftp客户端, 可以实现get指令下Tab键的快捷提示.

断点续传:
其中SIZE和REST的支持可以配合对应的client实现断点续传.

超时检测:
在PASV模式下, server会开放特定端口阻塞等待client连接. 如果客户端此时已经断开连接, 则server会陷入卡死. 本实验中实现的server在TCP accept过程中加入超时检测, 当超过1分钟没有客户端连接后, 将退出当前的操作.

断开检测:
Server在阻塞接受消息时, 会监控client的连接状态. 若client连接断开, 则关闭当前所有开启的接口, 并安全关闭若存在的DTP进程.

路径安全检查:
TODO 会展开

TODO 错误处理: 文件等,列一下 

## Client

### 基本结构
Client为C语言编写的命令行程序. 核心实现思路是, 尽量直接将用户输入的指令直接发送给server, 仅在必要时进行预处理和结果响应. 
开启client主进程时, 根据命令行参数指定的IP和port连接. 当连接成功后, client进入消息循环, 阻塞监听用户键盘输入, 并将每一次输入发送给server. 其中PORT, PASV, RETR, STOR, LIST, REST指令需要client进行必要操作, 并针对回复中的code对client进行设置.

其中PORT需要client监听特定端口. PASV需要记录server IP和端口. RETR, STOR, LIST需要开启子进程完成与server的连接和数据传输. REST需要记录指针移动位置. 以上指令均需要核对server回复中的code来判断client的设置是否批准.

当执行RETR, STOR和LIST时, 主进程会开辟DTP用户数据传输. 主进程专一维护control socket, DTP专一维护data socket. 
本实验中client实现了数据传输的人工中断. 当执行RETR, STOR, LIST时, client主进程监听键盘的ctrl+C操作. 当主进程监听到事件后, 首先向DTP传递SIGTERM消息. DTP中SIGTERM消息响应被重写, 收到消息后会搁置正在执行的数据传输或接收, 并监听与主进程的管道接口. 此时主进程向server发送ABOR请求. 若ABOR请求批准, 则主进程通过管道通知DTP安全结束进程. 若ABOR请求不批准, 则DTP回到中断位置重新接收数据.

### 实现功能
基本功能: USER, PASS, RETR, STOR, QUIT, SYST, TYPE, PORT, PASV, MKD, CWD, PWD, LIST, RMD.
额外功能: 
监听用户ctrl+C操作中断数据传输, 并发送ABOR指令给server
为简便调试和使用, PORT指令支持缺省参数. 此时client会随机选择尝试端口范围内的端口. 得到可用端口后, client代替用户补全指令中IP和端口, 并发送给server
REST: 设定文件传输指针位置. 结合server端SIZE指令或本地文件属性, 可以实现文件续传.

断点续传:
TODO

超时检测:
TODO


错误检测:
TODO


本机IP需要先写在程序内