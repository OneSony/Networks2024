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


  const ethernet_hdr *hdr = reinterpret_cast<const ethernet_hdr*>(packet);

  const Interface* iface = findIfaceByName(inIface);
  if (iface == nullptr) {
    std::cerr << "Received packet, but interface is unknown, ignoring" << std::endl;
    return;
  }else if(std::memcmp(hdr->ether_dhost, iface->addr.data(), ETHER_ADDR_LEN) != 0 && std::memcmp(hdr->ether_dhost, "\xff\xff\xff\xff\xff\xff", ETHER_ADDR_LEN) != 0){
    std::cerr << "Received packet, but not to this host, ignoring" << std::endl;
    return;
  }

  // handle ARP
  if (hdr->ether_type == ethertype_arp) {
    std::cerr << "Received ARP packet" << std::endl;
    const arp_hdr *arp = reinterpret_cast<const arp_hdr*>(packet + sizeof(ethernet_hdr));
    

    if(arp->arp_op == arp_op_request){
      std::cerr << "Received ARP request" << std::endl;

      if(arp->arp_tip == iface->ip && std::memcmp(hdr->ether_dhost, "\xff\xff\xff\xff\xff\xff", ETHER_ADDR_LEN) == 0){
        std::cerr << "ARP request for this host" << std::endl;

        ethernet_hdr eth_reply;
        memcpy(eth_reply.ether_dhost, arp->arp_sha, ETHER_ADDR_LEN);
        memcpy(eth_reply.ether_shost, iface->addr.data(), ETHER_ADDR_LEN);
        eth_reply.ether_type = ethertype_arp;

        arp_hdr arp_reply;
        arp_reply.arp_hrd = arp_hrd_ethernet;      // Ethernet
        arp_reply.arp_pro = 0x0800; // IPv4
        arp_reply.hlen = ETHER_ADDR_LEN;              // MAC 地址长度
        arp_reply.plen = 4;              // IPv4 地址长度
        arp_reply.op = arp_op_reply;
        memcpy(arp_reply.sha, iface->addr.data(), ETHER_ADDR_LEN);
        arp_reply.sip = iface->ip;
        memcpy(arp_reply.tha, arp->sha, ETHER_ADDR_LEN);
        arp_reply.tip = arp->arp_sip;

        Buffer packet_reply;
        packet_reply.insert(packet_reply.end(), (unsigned char*)&eth_reply, (unsigned char*)&eth_reply + sizeof(ethernet_hdr));
        packet_reply.insert(packet_reply.end(), (unsigned char*)&arp_reply, (unsigned char*)&arp_reply + sizeof(arp_hdr));

        sendPacket(packet_reply, inIface); //TODO??

      }else{
        std::cerr << "ARP request for another host" << std::endl;
        // do nothing
      }


    }else if(arp->arp_op == arp_op_reply){
      std::cerr << "Received ARP reply" << std::endl;

      if(arp->arp_tip == iface->ip && std::memcmp(hdr->ether_dhost, iface->addr.data(), ETHER_ADDR_LEN) == 0){
        Buffer mac_vector(ETHER_ADDR_LEN);
        std::memcpy(mac_vector.data(), arp->arp_sha, ETHER_ADDR_LEN);
        m_arp->insertArpEntry(mac_vector, arp->arp_sip);
      }else{
        std::cerr << "unknown ARP reply format" << std::endl;
        // do nothing
      }

    }


  //handle IP
  }else if (hdr->ether_type == ethertype_ip) {
    std::cerr << "Received IP packet" << std::endl;
    const ip_hdr *ip = reinterpret_cast<const ip_hdr*>(packet + sizeof(ethernet_hdr));


    // meets minimum length & checksum
    if(ip->ip_len < sizeof(ip_hdr)){
      std::cerr << "IP packet length is too short" << std::endl;
      return;
    }
    if(!varify_checksum(ip)){
      std::cerr << "Checksum is incorrect" << std::endl;
      return;
    }

    if(ip->ip_ttl == 0 || ip->ip_ttl == 1){
      std::cerr << "TTL is 0" << std::endl;
      return;
    }

    // 判断是否是到本机
    if(ip->ip_dst == iface->ip){

      //TODO ICMP

      //检查ip carry的是不是ICMP
      /*
        If an incoming IP packet is destined towards one of your router’s IP addresses, you should take the fol-
lowing actions, consistent with the section on protocols above:
• If the packet is an ICMP echo request and its checksum is valid, send an ICMP echo reply to the
sending host.
• If the packet contains a TCP or UDP payload, send an ICMP port unreachable to the sending host.
Otherwise, ignore the packet. Packets destined elsewhere should be forwarded using your normal
forwarding logic.
*/

    }else{
      //forward
      ip_hdr ip_forward = *ip;
      ip_forward.ip_ttl -= 1;
      update_checksum(&ip_forward);

      RoutingTableEntry next_hop = m_routingTable.lookup(ip->ip_dst);
      std::shared_ptr<ArpEntry> next_hop_ha = m_arp->lookup(next_hop.gw);

      if(next_hop_ha == nullptr){
        //缓存
        m_arp->queueRequest(next_hop.gw, packet, next_hop.ifName);
      }else{
        //直接转发
        ethernet_hdr eth_forward;
        memcpy(eth_forward.ether_dhost, next_hop_ha->mac.data(), ETHER_ADDR_LEN);
        memcpy(eth_forward.ether_shost, iface->addr.data(), ETHER_ADDR_LEN);
        eth_forward.ether_type = ethertype_ip;

        Buffer packet_forward;
        packet_forward.insert(packet_forward.end(), (unsigned char*)&eth_forward, (unsigned char*)&eth_forward + sizeof(ethernet_hdr));
        packet_forward.insert(packet_forward.end(), (unsigned char*)&ip_forward, (unsigned char*)&ip_forward + sizeof(ip_hdr));

        sendPacket(packet_forward, inIface);
      }
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
