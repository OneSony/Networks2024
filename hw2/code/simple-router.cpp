/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2017 Alexander Afanasyev
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation, either version
 * 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include "simple-router.hpp"
#include "core/utils.hpp"

#include <fstream>

namespace simple_router {

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
// IMPLEMENT THIS METHOD

void SimpleRouter::forwardPacket(const Buffer& packet, uint32_t src_ip, uint32_t dst_ip) { //这里添加以太网表头
  
  //src_ip用来构建ICMP

  auto is_myself = findIfaceByIp(dst_ip);
  if (is_myself != nullptr) {
    std::cerr << "To myself " << is_myself->name << std::endl;
    return;
  }

  RoutingTableEntry next_hop;    
  try{
    next_hop = m_routingTable.lookup(dst_ip);
  }catch(std::runtime_error e){
    std::cerr << "Routing entry not found forward packet" << std::endl;
    ip_hdr* ip = reinterpret_cast<ip_hdr*>(const_cast<unsigned char*>(packet.data()));

    //这里的packet是不是不包含以太网表头
    std::cerr << "ICMP host unreachable:" << "src " << ipToString(src_ip) << " dst " << ipToString(ip->ip_src) << std::endl;
    sendDataICMP(3, 1, src_ip, ip->ip_src, packet);
    return;
  }

  printf("next_hop: %s\n", ipToString(next_hop.gw).c_str());

  auto next_hop_ha = m_arp.lookup(next_hop.gw);
  auto iface = findIfaceByName(next_hop.ifName);

  if(next_hop_ha == nullptr){
    std::cerr << "main: ARP not found" << std::endl;
    std::cerr << "main: store request in " << next_hop.ifName << std::endl;
    m_arp.queueRequest(next_hop.gw, packet, next_hop.ifName);

  }else{
    std::cerr << "main: ARP found" << std::endl;

    ethernet_hdr eth;
    memcpy(eth.ether_shost, iface->addr.data(), ETHER_ADDR_LEN);
    memcpy(eth.ether_dhost, next_hop_ha->mac.data(), ETHER_ADDR_LEN);
    eth.ether_type = htons(ethertype_ip);

    Buffer packet_full;
    packet_full.insert(packet_full.end(), (unsigned char*)&eth, (unsigned char*)&eth + sizeof(ethernet_hdr));
    packet_full.insert(packet_full.end(), packet.begin(), packet.end());

    std::cerr << "main: sending to " << next_hop.ifName << std::endl;

    sendPacket(packet_full, next_hop.ifName);
  }
}

void SimpleRouter::sendDataICMP(int type, int code, uint32_t src_ip, uint32_t dst_ip, Buffer ori_packet) { //packet没有以太网头

  ip_hdr* ori_ip = reinterpret_cast<ip_hdr*>(const_cast<unsigned char*>(ori_packet.data()));
  
  icmp_data_hdr icmp;
  icmp.icmp_type = type;
  icmp.icmp_code = code;
  icmp.icmp_sum = 0;
  icmp.unused = 0;

  memcpy(icmp.data, ori_ip, sizeof(ip_hdr));

  size_t ip_payload_size = ntohs(ori_ip->ip_len) - sizeof(ip_hdr);
  size_t copy_size = (ip_payload_size < 8) ? ip_payload_size : 8;
  memcpy(icmp.data + sizeof(ip_hdr), ori_packet.data() + sizeof(ip_hdr), copy_size);

  if (ip_payload_size < 8) {
    memset(icmp.data + sizeof(ip_hdr) + copy_size, 0, 8 - copy_size);
  }

  icmp.icmp_sum = cksum(&icmp, sizeof(icmp_data_hdr)); //TODO checksum对吗


  srand(static_cast<unsigned int>(time(0)));
  ip_hdr ip;
  ip.ip_v = 4;
  ip.ip_hl = 5;
  ip.ip_tos = 0;
  ip.ip_len = htons(sizeof(ip_hdr) + sizeof(icmp_data_hdr));
  ip.ip_off = htons(IP_DF);
  ip.ip_id = htons(rand() % 65536);
  ip.ip_ttl = 64;
  ip.ip_p = ip_protocol_icmp;
  ip.ip_src = src_ip;
  ip.ip_dst = dst_ip;
  ip.ip_sum = 0;
  ip.ip_sum = cksum(&ip, sizeof(ip_hdr));


  Buffer packet_ttl;
  packet_ttl.insert(packet_ttl.end(), (unsigned char*)&ip, (unsigned char*)&ip + sizeof(ip_hdr));
  packet_ttl.insert(packet_ttl.end(), (unsigned char*)&icmp, (unsigned char*)&icmp + sizeof(icmp_data_hdr));

  forwardPacket(packet_ttl, src_ip, dst_ip);
}


void SimpleRouter::sendEchoICMP(int type, int code, uint32_t src_ip, uint32_t dst_ip, Buffer ori_packet) { //packet没有以太网头

  ip_hdr* ori_ip = reinterpret_cast<ip_hdr*>(const_cast<unsigned char*>(ori_packet.data()));

  //先拷贝一份
  Buffer icmp_reply_packet;
  icmp_reply_packet.insert(icmp_reply_packet.end(), ori_packet.begin() + sizeof(ip_hdr), ori_packet.begin() + ntohs(ori_ip->ip_len));
  
  //icmp_echo_hdr* icmp_ori = reinterpret_cast<icmp_echo_hdr*>(const_cast<unsigned char*>(ori_packet.data()) + sizeof(ip_hdr));
  icmp_echo_hdr* icmp_reply = reinterpret_cast<icmp_echo_hdr*>(icmp_reply_packet.data());
  icmp_reply->icmp_type = 0;
  icmp_reply->icmp_sum = 0;
  //icmp_reply->seq = htons(ntohs(icmp_ori->seq) + 1); 不需要加1啊

  icmp_reply->icmp_sum = cksum(icmp_reply_packet.data(), icmp_reply_packet.size());

  srand(static_cast<unsigned int>(time(0)));
  ip_hdr ip;
  ip.ip_v = 4;
  ip.ip_hl = 5;
  ip.ip_tos = 0;
  ip.ip_len = htons(sizeof(ip_hdr) + icmp_reply_packet.size());
  ip.ip_off = htons(IP_DF);
  ip.ip_id = htons(rand() % 65536);
  ip.ip_ttl = 64;
  ip.ip_p = ip_protocol_icmp;
  ip.ip_src = src_ip;
  ip.ip_dst = dst_ip;
  ip.ip_sum = 0;
  ip.ip_sum = cksum(&ip, sizeof(ip_hdr));

  Buffer packet;
  packet.insert(packet.end(), (unsigned char*)&ip, (unsigned char*)&ip + sizeof(ip_hdr));
  packet.insert(packet.end(), icmp_reply_packet.begin(), icmp_reply_packet.end());

  forwardPacket(packet, src_ip, dst_ip);
}

void
SimpleRouter::handlePacket(const Buffer& packet, const std::string& inIface)
{
  std::cerr << "Got packet of size " << packet.size() << " on interface " << inIface << std::endl;


  const ethernet_hdr *hdr = reinterpret_cast<const ethernet_hdr*>(packet.data());


  // to me?
  //TODO: check if the packet is for me
  
  const Interface* iface = findIfaceByName(inIface);
  
  //print_hdr_eth(packet.data());

  if (iface == nullptr) {
    std::cerr << "Received packet, but interface is unknown, ignoring" << std::endl;
    return;
  }

  // handle ARP
  // ARP只需要管自己子网的
  if (ntohs(hdr->ether_type) == ethertype_arp) {
    std::cerr << "Received ARP packet" << std::endl;
    const arp_hdr *arp = reinterpret_cast<const arp_hdr*>(packet.data() + sizeof(ethernet_hdr));
    
    if(ntohs(arp->arp_op) == arp_op_request){
      std::cerr << "Received ARP request" << std::endl;

      if(arp->arp_tip == iface->ip && std::memcmp(hdr->ether_dhost, "\xff\xff\xff\xff\xff\xff", ETHER_ADDR_LEN) == 0){
        std::cerr << "ARP request for this host" << std::endl;

        ethernet_hdr eth_reply;
        memcpy(eth_reply.ether_dhost, arp->arp_sha, ETHER_ADDR_LEN);
        memcpy(eth_reply.ether_shost, iface->addr.data(), ETHER_ADDR_LEN);
        eth_reply.ether_type = htons(ethertype_arp);

        arp_hdr arp_reply;
        arp_reply.arp_hrd = htons(arp_hrd_ethernet);      // Ethernet
        arp_reply.arp_pro = htons(0x0800); // IPv4
        arp_reply.arp_hln = ETHER_ADDR_LEN;              // MAC 地址长度
        arp_reply.arp_pln = 4;              // IPv4 地址长度
        arp_reply.arp_op = htons(arp_op_reply);
        memcpy(arp_reply.arp_sha, iface->addr.data(), ETHER_ADDR_LEN);
        arp_reply.arp_sip = iface->ip;
        memcpy(arp_reply.arp_tha, arp->arp_sha, ETHER_ADDR_LEN);
        arp_reply.arp_tip = arp->arp_sip;

        Buffer packet_reply;
        packet_reply.insert(packet_reply.end(), (unsigned char*)&eth_reply, (unsigned char*)&eth_reply + sizeof(ethernet_hdr));
        packet_reply.insert(packet_reply.end(), (unsigned char*)&arp_reply, (unsigned char*)&arp_reply + sizeof(arp_hdr));

        sendPacket(packet_reply, inIface); //TODO 直接原路返回
        
      }else{
        std::cerr << "ARP request for another host" << std::endl;
        // do nothing
      }


    }else if(ntohs(arp->arp_op) == arp_op_reply){
      std::cerr << "Received ARP reply" << std::endl;

      if(arp->arp_tip == iface->ip && std::memcmp(hdr->ether_dhost, iface->addr.data(), ETHER_ADDR_LEN) == 0){
        Buffer mac_vector(ETHER_ADDR_LEN);
        std::memcpy(mac_vector.data(), arp->arp_sha, ETHER_ADDR_LEN);
        auto arp_requests = m_arp.insertArpEntry(mac_vector, arp->arp_sip);
        m_arp.removeRequest(arp_requests); //先从队列中删除, 避免重复ARP retry

        if(arp_requests == nullptr){
          std::cerr << "ARP has already been handled" << std::endl;
          return;
        }

        for(auto packet_it = arp_requests->packets.begin(); packet_it != arp_requests->packets.end(); packet_it++) {
          //发送packet
          Buffer packet = packet_it->packet;

          //std::cerr << "!!!packet_size" << packet.size()<< std::endl;

          ethernet_hdr eth;
          memcpy(eth.ether_shost, iface->addr.data(), ETHER_ADDR_LEN);
          memcpy(eth.ether_dhost, mac_vector.data(), ETHER_ADDR_LEN);
          eth.ether_type = htons(ethertype_ip);

          packet.insert(packet.begin(), (unsigned char*)&eth, (unsigned char*)&eth + sizeof(ethernet_hdr));

          std::string iface = packet_it->iface;

          std::cerr<< "sending to " << iface <<std::endl;

          sendPacket(packet, iface);
        }

      }else{
        std::cerr << "unknown ARP reply format" << std::endl;
        // do nothing
      }

    }else{
      std::cerr << "unknown ARP format" << std::endl;
    }


  //handle IP
  }else if (ntohs(hdr->ether_type) == ethertype_ip) {
    std::cerr << "Received IP packet" << std::endl;
    const ip_hdr *ip = reinterpret_cast<const ip_hdr*>(packet.data() + sizeof(ethernet_hdr));

    //print_hdr_eth(packet.data());
    //print_hdr_ip(packet.data()+sizeof(ethernet_hdr));


    // meets minimum length & checksum
    if(ntohs(ip->ip_len) < sizeof(ip_hdr)){
      std::cerr << "IP packet length is too short" << std::endl;
      return;
    }

    uint16_t checksum=cksum(ip, sizeof(ip_hdr));
    if(checksum!=0xffff){
      std::cerr << "IP checksum is incorrect" << std::endl;
      return;
    }

    std::cerr << "IP is correct" << std::endl;

    std::cerr << "Destination IP: " << ipToString(ip->ip_dst) << std::endl;
    std::cerr << "This IP: " << ipToString(iface->ip) << std::endl;


    // 遍历接口

    auto dst_iface = findIfaceByIp(ip->ip_dst);

    // 判断是否是到本机
    if(dst_iface != nullptr){

      std::cerr << "Packet is for me" << std::endl;

      if(ip->ip_p == ip_protocol_icmp){

        //不需要检查TTL, 因为只要收到了就是TTL>0, 这是面向本机的, 不需要转发

        print_hdr_echo_icmp(packet.data() + sizeof(ethernet_hdr) + sizeof(ip_hdr));

        //通用ICMP表头
        icmp_hdr* icmp = reinterpret_cast<icmp_hdr*>(const_cast<unsigned char*>(packet.data() + sizeof(ethernet_hdr) + sizeof(ip_hdr)));
        uint16_t checksum=cksum(icmp, ntohs(ip->ip_len) - sizeof(ip_hdr));
        if(checksum!=0xffff){ //cksum如果是0返回0xffff
          std::cerr << "ICMP checksum is incorrect" << std::endl;
          return;
        }

        std::cerr << "ICMP is correct" << std::endl;

        Buffer ip_packet;
        ip_packet.insert(ip_packet.end(), packet.begin() + sizeof(ethernet_hdr), packet.end());

        sendEchoICMP(0, 0, ip->ip_dst, ip->ip_src, ip_packet);

      }else if(ip->ip_p == 6 || ip->ip_p == 17){
        //TCP or UDP
        std::cerr << "TCP or UDP" << std::endl;

        Buffer ip_packet;
        ip_packet.insert(ip_packet.end(), packet.begin() + sizeof(ethernet_hdr), packet.end());
        
        sendDataICMP(3, 3, ip->ip_dst, ip->ip_src, ip_packet); //TODO 需要检查

      }else{
        std::cerr << "Unknown protocol" << std::endl;
      }

    }else{

      if(ip->ip_ttl == 0 || ip->ip_ttl == 1){
        std::cerr << "TTL is 0" << std::endl;
        //TODO 附带的data是TTL多少

        Buffer ip_packet;
        ip_packet.insert(ip_packet.end(), packet.begin() + sizeof(ethernet_hdr), packet.end());
        
        sendDataICMP(11, 0, iface->ip, ip->ip_src, ip_packet);
        return;
      }

      std::cerr<<"Forwarding packet"<<std::endl;

      //forward的packet不需要知道本机ip

      ip_hdr ip_forward = *ip;
      ip_forward.ip_ttl -= 1;
      ip_forward.ip_sum = 0;
      ip_forward.ip_sum = cksum(&ip_forward, sizeof(ip_hdr));
      

      Buffer packet_forward;
      packet_forward.insert(packet_forward.end(), (unsigned char*)&ip_forward, (unsigned char*)&ip_forward + sizeof(ip_hdr));
      packet_forward.insert(packet_forward.end(), packet.begin() + sizeof(ethernet_hdr) + sizeof(ip_hdr), packet.end());
      
      forwardPacket(packet_forward, iface->ip, ip_forward.ip_dst);

    }
  }else {
    std::cerr << "Received unknown packet" << std::endl;
  }

  //std::cerr << getRoutingTable() << std::endl;

  // FILL THIS IN

}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

// You should not need to touch the rest of this code.
SimpleRouter::SimpleRouter()
  : m_arp(*this)
{
}

void
SimpleRouter::sendPacket(const Buffer& packet, const std::string& outIface)
{
  m_pox->begin_sendPacket(packet, outIface);
}

bool
SimpleRouter::loadRoutingTable(const std::string& rtConfig)
{
  return m_routingTable.load(rtConfig);
}

void
SimpleRouter::loadIfconfig(const std::string& ifconfig)
{
  std::ifstream iff(ifconfig.c_str());
  std::string line;
  while (std::getline(iff, line)) {
    std::istringstream ifLine(line);
    std::string iface, ip;
    ifLine >> iface >> ip;

    in_addr ip_addr;
    if (inet_aton(ip.c_str(), &ip_addr) == 0) {
      throw std::runtime_error("Invalid IP address `" + ip + "` for interface `" + iface + "`");
    }

    m_ifNameToIpMap[iface] = ip_addr.s_addr;
  }
}

void
SimpleRouter::printIfaces(std::ostream& os)
{
  if (m_ifaces.empty()) {
    os << " Interface list empty " << std::endl;
    return;
  }

  for (const auto& iface : m_ifaces) {
    os << iface << "\n";
  }
  os.flush();
}

const Interface*
SimpleRouter::findIfaceByIp(uint32_t ip) const
{
  auto iface = std::find_if(m_ifaces.begin(), m_ifaces.end(), [ip] (const Interface& iface) {
      return iface.ip == ip;
    });

  if (iface == m_ifaces.end()) {
    return nullptr;
  }

  return &*iface;
}

const Interface*
SimpleRouter::findIfaceByMac(const Buffer& mac) const
{
  auto iface = std::find_if(m_ifaces.begin(), m_ifaces.end(), [mac] (const Interface& iface) {
      return iface.addr == mac;
    });

  if (iface == m_ifaces.end()) {
    return nullptr;
  }

  return &*iface;
}

const Interface*
SimpleRouter::findIfaceByName(const std::string& name) const
{
  auto iface = std::find_if(m_ifaces.begin(), m_ifaces.end(), [name] (const Interface& iface) {
      return iface.name == name;
    });

  if (iface == m_ifaces.end()) {
    return nullptr;
  }

  return &*iface;
}

void
SimpleRouter::reset(const pox::Ifaces& ports)
{
  std::cerr << "Resetting SimpleRouter with " << ports.size() << " ports" << std::endl;

  m_arp.clear();
  m_ifaces.clear();

  for (const auto& iface : ports) {
    auto ip = m_ifNameToIpMap.find(iface.name);
    if (ip == m_ifNameToIpMap.end()) {
      std::cerr << "IP_CONFIG missing information about interface `" + iface.name + "`. Skipping it" << std::endl;
      continue;
    }

    m_ifaces.insert(Interface(iface.name, iface.mac, ip->second));
  }

  printIfaces(std::cerr);
}


} // namespace simple_router {
