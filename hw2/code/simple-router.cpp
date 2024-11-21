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

void SimpleRouter::forwardPacket(const Buffer& packet, uint32_t dst_ip) {
  
  RoutingTableEntry next_hop;    
  try{
    next_hop = m_routingTable.lookup(dst_ip);
  }catch(std::runtime_error e){
    std::cerr << "Routing entry not found" << std::endl;
    //TODO ICMP
    return;
  }

  printf("next_hop: %s\n", ipToString(next_hop.gw).c_str());

  auto next_hop_ha = m_arp.lookup(next_hop.gw);

  if(next_hop_ha == nullptr){
    std::cerr << "ARP not found" << std::endl;
    std::cerr << "store request in " << next_hop.ifName << std::endl;
    m_arp.queueRequest(next_hop.gw, packet, next_hop.ifName);

  }else{
    std::cerr << "ARP found" << std::endl;
    ethernet_hdr* eth = reinterpret_cast<ethernet_hdr*>(const_cast<unsigned char*>(packet.data()));
    memcpy(eth->ether_dhost, next_hop_ha->mac.data(), ETHER_ADDR_LEN);

    std::cerr << "sending to " << next_hop.ifName << std::endl;

    sendPacket(packet, next_hop.ifName);
  }
}

bool verify_checksum(const ip_hdr* header) {
    uint32_t sum = 0;
    const uint16_t* ptr = reinterpret_cast<const uint16_t*>(header);

    // 遍历每个 16 位单元
    for (size_t i = 0; i < sizeof(ip_hdr) / 2; ++i) {
        sum += *ptr++;
    }

    // 处理高位进位
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    // 如果校验和结果为 0xFFFF，则表示正确
    return sum == 0xFFFF;
}


// Function to calculate the checksum
void update_checksum(ip_hdr* header) {
    uint32_t sum = 0;

    header->ip_sum = 0;

    const uint16_t* ptr = reinterpret_cast<const uint16_t*>(header);

    // 遍历每个 16 位单元
    for (size_t i = 0; i < sizeof(ip_hdr) / 2; ++i) {
        sum += *ptr++;
    }

    // 处理高位进位
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    header->ip_sum = static_cast<uint16_t>(~sum);
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
  if (ntohs(hdr->ether_type) == ethertype_arp) {
    std::cerr << "Received ARP packet" << std::endl;
    const arp_hdr *arp = reinterpret_cast<const arp_hdr*>(packet.data() + sizeof(ethernet_hdr));
    
    print_hdr_eth(packet.data());
    print_hdr_arp(packet.data()+sizeof(ethernet_hdr));

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

        for(auto packet_it = arp_requests->packets.begin(); packet_it != arp_requests->packets.end(); packet_it++) {
          //发送packet
          Buffer packet = packet_it->packet;
          ethernet_hdr* eth = reinterpret_cast<ethernet_hdr*>(packet.data());
          memcpy(eth->ether_dhost, mac_vector.data(), ETHER_ADDR_LEN);

          std::string iface = packet_it->iface;

          std::cerr<< "sending to " << iface <<std::endl;

          sendPacket(packet, iface);
        }
        m_arp.removeRequest(arp_requests);

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

    print_hdr_eth(packet.data());
    print_hdr_ip(packet.data()+sizeof(ethernet_hdr));


    // meets minimum length & checksum
    if(ntohs(ip->ip_len) < sizeof(ip_hdr)){
      std::cerr << "IP packet length is too short" << std::endl;
      return;
    }
    if(!verify_checksum(ip)){
      std::cerr << "Checksum is incorrect" << std::endl;
      return;
    }

    if(ip->ip_ttl == 0 || ip->ip_ttl == 1){
      std::cerr << "TTL is 0" << std::endl;

      //TODO 附带的data是TTL多少

      icmp_t11_hdr icmp_ttl;
      icmp_ttl.icmp_type = 11;
      icmp_ttl.icmp_code = 0;
      icmp_ttl.icmp_sum = 0;
      icmp_ttl.unused = 0;

      memcpy(icmp_ttl.data, ip, sizeof(ip_hdr));

      size_t ip_payload_size = ntohs(ip->ip_len) - sizeof(ip_hdr);
      size_t copy_size = (ip_payload_size < 8) ? ip_payload_size : 8;
      memcpy(icmp_ttl.data + sizeof(ip_hdr), packet.data() + sizeof(ethernet_hdr) + sizeof(ip_hdr), copy_size);

      if (ip_payload_size < 8) {
        memset(icmp_ttl.data + sizeof(ip_hdr) + copy_size, 0, 8 - copy_size);
      }

      icmp_ttl.icmp_sum = cksum(&icmp_ttl, sizeof(icmp_t11_hdr)); //TODO

      srand(static_cast<unsigned int>(time(0)));
      ip_hdr ip_ttl;
      ip_ttl.ip_v = 4;
      ip_ttl.ip_hl = 5;
      ip_ttl.ip_tos = 0;
      ip_ttl.ip_len = htons(sizeof(ip_hdr) + sizeof(icmp_t11_hdr));
      ip_ttl.ip_off = htons(IP_DF);
      ip_ttl.ip_id = htons(rand() % 65536);
      ip_ttl.ip_ttl = 64;
      ip_ttl.ip_p = ip_protocol_icmp;
      ip_ttl.ip_sum = 0;
      ip_ttl.ip_src = iface->ip;
      ip_ttl.ip_dst = ip->ip_src;
      update_checksum(&ip_ttl);

      ethernet_hdr eth_ttl;
      memcpy(eth_ttl.ether_shost, iface->addr.data(), ETHER_ADDR_LEN);
      eth_ttl.ether_type = htons(ethertype_ip);

      Buffer packet_ttl;
      packet_ttl.insert(packet_ttl.end(), (unsigned char*)&eth_ttl, (unsigned char*)&eth_ttl + sizeof(ethernet_hdr));
      packet_ttl.insert(packet_ttl.end(), (unsigned char*)&ip_ttl, (unsigned char*)&ip_ttl + sizeof(ip_hdr));
      packet_ttl.insert(packet_ttl.end(), (unsigned char*)&icmp_ttl, (unsigned char*)&icmp_ttl + sizeof(icmp_t11_hdr));

      //print_hdr_eth(packet_ttl.data());
      //print_hdr_ip(packet_ttl.data()+sizeof(ethernet_hdr));
      //print_hdr_icmp(packet_ttl.data()+sizeof(ethernet_hdr)+sizeof(ip_hdr));
      //print_hdr_ip(packet_ttl.data()+sizeof(ethernet_hdr)+sizeof(ip_hdr)+sizeof(icmp_t11_hdr)-28);

      forwardPacket(packet_ttl, ip_ttl.ip_dst);
      return;
    }

    std::cerr << "IP is correct" << std::endl;

    // 判断是否是到本机
    if(ip->ip_dst == iface->ip){

      if(ip->ip_p == ip_protocol_icmp){
        //ICMP

        //const icmp_hdr* icmp = reinterpret_cast<const icmp_hdr*>(packet.data() + sizeof(ethernet_hdr) + sizeof(ip_hdr));

        //TODO
        /*
        • If the packet is an ICMP echo request and its checksum is valid, send an ICMP echo reply to the
        sending host.
        */

      }else if(ip->ip_p == 6 || ip->ip_p == 17){
        //TCP or UDP
        std::cerr << "TCP or UDP" << std::endl;

        /*
        • If the packet contains a TCP or UDP payload, send an ICMP port unreachable to the sending host.
        Otherwise, ignore the packet. Packets destined elsewhere should be forwarded using your normal
        forwarding logic.
        */

        //TODO 
      }else{
        std::cerr << "Unknown protocol" << std::endl;
      }

    }else{

      std::cerr<<"Forwarding packet"<<std::endl;

      ip_hdr ip_forward = *ip;
      ip_forward.ip_ttl -= 1;
      update_checksum(&ip_forward);
      
      ethernet_hdr eth_forward;
      //memcpy(eth_forward.ether_dhost, next_hop_ha->mac.data(), ETHER_ADDR_LEN);
      memcpy(eth_forward.ether_shost, iface->addr.data(), ETHER_ADDR_LEN);
      eth_forward.ether_type = htons(ethertype_ip);

      Buffer packet_forward;
      packet_forward.insert(packet_forward.end(), (unsigned char*)&eth_forward, (unsigned char*)&eth_forward + sizeof(ethernet_hdr));
      packet_forward.insert(packet_forward.end(), (unsigned char*)&ip_forward, (unsigned char*)&ip_forward + sizeof(ip_hdr));
      packet_forward.insert(packet_forward.end(), packet.begin() + sizeof(ethernet_hdr) + sizeof(ip_hdr), packet.end());
      
      forwardPacket(packet_forward, ip_forward.ip_dst);

    }
  }else {
    std::cerr << "Received unknown packet" << std::endl;
  }

  std::cerr << getRoutingTable() << std::endl;

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
