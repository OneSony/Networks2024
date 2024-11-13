import networkx as nx
import matplotlib.pyplot as plt
import re

# 解析 traceroute 输出文件
def parse_traceroute_output(file_path):
    hops = []
    with open(file_path, 'r') as f:
        for line in f:
            # 检查是否是新的 traceroute 输出头行
            match_header = re.match(r'traceroute to ([\w\.]+)', line)
            if match_header:
                # 如果当前已有跳数数据，则返回当前解析结果
                if hops:
                    yield hops  # 使用 generator 来返回每次解析的 hops 数据
                hops = []  # 重置 hops 为下一个traceroute的跳数数据
                continue
            
            # 使用正则表达式提取每一跳的 IP 地址
            match = re.match(r'\s*(\d+)\s+([\d\.]+)', line)
            if match:
                hop_number = int(match.group(1))
                ip_address = match.group(2)
                hops.append((hop_number, ip_address))
    
    # 在文件结束后返回最后一个解析的 hops
    if hops:
        yield hops

# 创建网络图
def add_network_edge(G, hops, color):
    for i in range(1, len(hops)):
        if(i > 6):
            break
        source = hops[i-1][1]  # 前一个跳的 IP 地址
        target = hops[i][1]  # 当前跳的 IP 地址
        # 添加多重边，每条边用唯一的键标记，并设定颜色
        G.add_edge(source, target, color=color, key=len(G.edges(source, target)))
    return G

# 绘制网络拓扑图
def draw_graph(G, filename):
    plt.figure(figsize=(20, 20))
    pos = nx.spring_layout(G, seed=10)
    
    # 绘制节点
    nx.draw_networkx_nodes(G, pos, node_size=200, node_color='skyblue')
    nx.draw_networkx_labels(G, pos, font_size=10, font_weight='bold')

    # 根据边的多重性，设置不同的弧度
    for i, (u, v, k) in enumerate(G.edges(keys=True)):
        color = G[u][v][k]['color']
        # 使用 `rad` 参数为不同的多重边设置弧度，避免重叠
        rad = 0.2 * (k - (len(G.edges(u, v)) - 1) / 2)
        nx.draw_networkx_edges(G, pos, edgelist=[(u, v)], edge_color=color, connectionstyle=f"arc3,rad={rad}")
    
    #plt.title('Traceroute Network Topology')
    plt.savefig(filename, format='PNG')
    plt.close()

# 主程序
if __name__ == '__main__':
    G = nx.MultiDiGraph()  # 使用有向多重图
    
    file_paths = [
        '/home/liuyun/wsl-code/Networks/traceroute/traceroute_inside_six.log',
        '/home/liuyun/wsl-code/Networks/traceroute/traceroute_outside_six.log',
        '/home/liuyun/wsl-code/Networks/traceroute/traceroute_inside_dorm.log',
        '/home/liuyun/wsl-code/Networks/traceroute/traceroute_outside_dorm.log',
    ]
    
    # 定义边颜色的映射（你可以根据需要调整颜色）
    color_map = {
        '/home/liuyun/wsl-code/Networks/traceroute/traceroute_inside_six.log': 'purple',
        '/home/liuyun/wsl-code/Networks/traceroute/traceroute_outside_six.log': 'gray',
        '/home/liuyun/wsl-code/Networks/traceroute/traceroute_inside_dorm.log': 'purple',
        '/home/liuyun/wsl-code/Networks/traceroute/traceroute_outside_dorm.log': 'gray',
    }
    
    # 逐个解析文件中的每个traceroute输出并添加到网络图中
    for file_path in file_paths:
        for hops in parse_traceroute_output(file_path):
            color = color_map[file_path]  # 根据文件路径获取颜色
            G = add_network_edge(G, hops, color)
    
    output_filename = '/home/liuyun/wsl-code/Networks/traceroute/outin.png'
    draw_graph(G, output_filename)
