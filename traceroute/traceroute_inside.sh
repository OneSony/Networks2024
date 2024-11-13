urls=(
    "info.tsinghua.edu.cn"
    "learn.tsinghua.edu.cn"
    "ereserves.lib.tsinghua.edu.cn"
    "zhjwxk.cic.tsinghua.edu.cn"
    "sa.tsinghua.edu.cn"
)

> traceroute_inside.log

for url in "${urls[@]}"
do
    traceroute -n -m 30 $url >> traceroute_inside_dorm.log
done