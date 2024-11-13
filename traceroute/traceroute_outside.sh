urls=(
    "www.baidu.com"
    "www.qq.com"
    "www.taobao.com"
    "www.sina.com.cn"
    "www.weibo.com"
)

> traceroute_outside.log

for url in "${urls[@]}"
do
    traceroute -n -m 15 $url >> traceroute_outside_dorm.log
done