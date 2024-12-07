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

#include "arp-cache.hpp"
#include "core/utils.hpp"
#include "core/interface.hpp"
#include "simple-router.hpp"

#include <algorithm>
#include <iostream>

namespace simple_router {

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
// IMPLEMENT THIS METHOD
void
ArpCache::periodicCheckArpRequestsAndCacheEntries()
{
  //调用时已上锁
  //不能调用函数remove, remove有锁, 会死锁

  for(auto it = m_cacheEntries.begin(); it != m_cacheEntries.end();) {
    //在调用前已经更新了valid

    //std::cerr<<"ARP cache checking " << ipToString((*it)->ip) <<std::endl;
    if((*it)->isValid == false) {
      it = m_cacheEntries.erase(it);
    }else{
      it++;
    }
  }

  for(auto it = m_arpRequests.begin(); it != m_arpRequests.end();) {
    //不能lookup, 会死锁

    if(steady_clock::now() - (*it)->timeSent > seconds(1)) {
      if((*it)->nTimesSent >= 5) {
        //如果发送了5次了，那么就要发送ICMP host unreachable
        for(auto packet_it = (*it)->packets.begin(); packet_it != (*it)->packets.end(); packet_it++) {
          //发送ICMP host unreachable
          std::cerr<<"ARP request timeout"<<std::endl;

          //在本实验中只有ip包会被加入队列, 所以packet一定有ip头
          //不能调用内部的函数，因为内部的函数会加锁
          const ip_hdr *ori_ip = reinterpret_cast<const ip_hdr*>(packet_it->packet.data()); //注意这里没有以太网包头

          //std::cerr << "packet size!!!!!!!!!???"<< packet_it->packet.size() << std::endl;

          auto iface = m_router.findIfaceByName(packet_it->iface);
          
          std::cerr<<"ICMP host unreachable:"<< "src " << ipToString(iface->ip) << " dst " << ipToString(ori_ip->ip_src) <<std::endl;
          
          uint32_t src_ip = iface->ip;
          uint32_t dst_ip = ori_ip->ip_src;

          icmp_data_hdr icmp;
          icmp.icmp_type = 3;
          icmp.icmp_code = 1;
          icmp.icmp_sum = 0;
          icmp.unused = 0;

          memcpy(icmp.data, ori_ip, sizeof(ip_hdr));
          //std::cerr<<"!!!!!!:::ip size"<<sizeof(ip_hdr)<<std::endl;
          print_hdr_ip(icmp.data);

          size_t ip_payload_size = ntohs(ori_ip->ip_len) - sizeof(ip_hdr);
          //std::cerr<<"!!!!!!:::ip_payload_size"<<ip_payload_size<<std::endl;
          size_t copy_size = (ip_payload_size < 8) ? ip_payload_size : 8;
          //std::cerr<<"!!!!!!:::copy_size"<<copy_size<<std::endl;
          memcpy(icmp.data + sizeof(ip_hdr), packet_it->packet.data() + sizeof(ip_hdr), copy_size);

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

          Buffer packet_unreachable;
          packet_unreachable.insert(packet_unreachable.end(), (unsigned char*)&ip, (unsigned char*)&ip + sizeof(ip_hdr));
          packet_unreachable.insert(packet_unreachable.end(), (unsigned char*)&icmp, (unsigned char*)&icmp + sizeof(icmp_data_hdr));

          RoutingTableEntry next_hop;
          try{
            next_hop = m_router.getRoutingTable().lookup(dst_ip);
          }catch(std::runtime_error e){
            std::cerr << "Routing entry not found" << std::endl;
            //do nothing
            break;
          }

          std::shared_ptr<ArpEntry> next_hop_ha = nullptr; //寻找ARP
          //寻找ARP
          for (const auto& entry : m_cacheEntries) {
            if (entry->isValid && entry->ip == next_hop.gw) {
              next_hop_ha = entry;
              break;
            }
          }


          if(next_hop_ha == nullptr){
            std::cerr << "ARP not found" << std::endl;
            std::cerr << "store request in " << next_hop.ifName << std::endl;

            queueRequest_inside(next_hop.gw, packet_unreachable, next_hop.ifName);
          }else{
            std::cerr << "ARP found" << std::endl;

            ethernet_hdr eth;
            memcpy(eth.ether_shost, iface->addr.data(), ETHER_ADDR_LEN);
            memcpy(eth.ether_dhost, next_hop_ha->mac.data(), ETHER_ADDR_LEN);
            eth.ether_type = htons(ethertype_ip);

            Buffer packet_full;
            packet_full.insert(packet_full.end(), (unsigned char*)&eth, (unsigned char*)&eth + sizeof(ethernet_hdr));
            packet_full.insert(packet_full.end(), packet_unreachable.begin(), packet_unreachable.end());

            std::cerr << "sending to " << next_hop.ifName << std::endl;
            m_router.sendPacket(packet_full, next_hop.ifName);
          }

        }
        //删除这个请求
        it = m_arpRequests.erase(it);
      }else{
        //继续发送ARP请求

        auto iface = m_router.findIfaceByName((*it)->packets.front().iface); //TODO!!!!只看第一个人的iface?

        ethernet_hdr eth;
        memcpy(eth.ether_dhost, "\xff\xff\xff\xff\xff\xff", ETHER_ADDR_LEN);
        memcpy(eth.ether_shost, iface->addr.data(), ETHER_ADDR_LEN);

        eth.ether_type = htons(ethertype_arp);

        arp_hdr arp_request;
        arp_request.arp_hrd = htons(arp_hrd_ethernet);      // Ethernet
        arp_request.arp_pro = htons(0x0800); // IPv4
        arp_request.arp_hln = ETHER_ADDR_LEN;              // MAC 地址长度
        arp_request.arp_pln = 4;              // IPv4 地址长度
        arp_request.arp_op = htons(arp_op_request);
        memcpy(arp_request.arp_sha, iface->addr.data(), ETHER_ADDR_LEN);
        arp_request.arp_sip = iface->ip;
        memcpy(arp_request.arp_tha, "\xff\xff\xff\xff\xff\xff", ETHER_ADDR_LEN);
        arp_request.arp_tip = (*it)->ip;

        Buffer packet_request;
        packet_request.insert(packet_request.end(), (unsigned char*)&eth, (unsigned char*)&eth + sizeof(ethernet_hdr));
        packet_request.insert(packet_request.end(), (unsigned char*)&arp_request, (unsigned char*)&arp_request + sizeof(arp_hdr));

        //std::cerr<<"ARP request making"<<std::endl;
        //print_hdr_eth(packet_request.data());
        //print_hdr_arp(packet_request.data()+sizeof(ethernet_hdr));

        m_router.sendPacket(packet_request, iface->name);

        (*it)->timeSent = steady_clock::now();
        (*it)->nTimesSent++;
        it++;
      }
    }
  }

}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

// You should not need to touch the rest of this code.

ArpCache::ArpCache(SimpleRouter& router)
  : m_router(router)
  , m_shouldStop(false)
  , m_tickerThread(std::bind(&ArpCache::ticker, this))
{
}

ArpCache::~ArpCache()
{
  m_shouldStop = true;
  m_tickerThread.join();
}

std::shared_ptr<ArpEntry>
ArpCache::lookup(uint32_t ip)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  for (const auto& entry : m_cacheEntries) {
    if (entry->isValid && entry->ip == ip) {
      return entry;
    }
  }

  return nullptr;
}

std::shared_ptr<ArpRequest>
ArpCache::queueRequest(uint32_t ip, const Buffer& packet, const std::string& iface)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  auto request = std::find_if(m_arpRequests.begin(), m_arpRequests.end(),
                           [ip] (const std::shared_ptr<ArpRequest>& request) {
                             return (request->ip == ip);
                           });

  if (request == m_arpRequests.end()) {
    request = m_arpRequests.insert(m_arpRequests.end(), std::make_shared<ArpRequest>(ip));
  }

  // Add the packet to the list of packets for this request
  (*request)->packets.push_back({packet, iface});
  return *request;
}

std::shared_ptr<ArpRequest>
ArpCache::queueRequest_inside(uint32_t ip, const Buffer& packet, const std::string& iface)
{

  auto request = std::find_if(m_arpRequests.begin(), m_arpRequests.end(),
                           [ip] (const std::shared_ptr<ArpRequest>& request) {
                             return (request->ip == ip);
                           });

  if (request == m_arpRequests.end()) {
    request = m_arpRequests.insert(m_arpRequests.end(), std::make_shared<ArpRequest>(ip));
  }

  // Add the packet to the list of packets for this request
  (*request)->packets.push_back({packet, iface});
  return *request;
}

void
ArpCache::removeRequest(const std::shared_ptr<ArpRequest>& entry)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_arpRequests.remove(entry);
}

std::shared_ptr<ArpRequest>
ArpCache::insertArpEntry(const Buffer& mac, uint32_t ip)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  auto entry = std::make_shared<ArpEntry>();
  entry->mac = mac;
  entry->ip = ip;
  entry->timeAdded = steady_clock::now();
  entry->isValid = true;
  m_cacheEntries.push_back(entry);

  auto request = std::find_if(m_arpRequests.begin(), m_arpRequests.end(),
                           [ip] (const std::shared_ptr<ArpRequest>& request) {
                             return (request->ip == ip);
                           });
  if (request != m_arpRequests.end()) {
    return *request;
  }
  else {
    return nullptr;
  }
}

void
ArpCache::clear()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  m_cacheEntries.clear();
  m_arpRequests.clear();
}

void
ArpCache::ticker()
{
  while (!m_shouldStop) {
    std::this_thread::sleep_for(std::chrono::seconds(1));

    {
      std::lock_guard<std::mutex> lock(m_mutex);

      auto now = steady_clock::now();

      for (auto& entry : m_cacheEntries) {
        if (entry->isValid && (now - entry->timeAdded > SR_ARPCACHE_TO)) {
          entry->isValid = false;
        }
      }

      periodicCheckArpRequestsAndCacheEntries();
    }
  }
}

std::ostream&
operator<<(std::ostream& os, const ArpCache& cache)
{
  std::lock_guard<std::mutex> lock(cache.m_mutex);

  os << "\nMAC            IP         AGE                       VALID\n"
     << "-----------------------------------------------------------\n";

  auto now = steady_clock::now();
  for (const auto& entry : cache.m_cacheEntries) {

    os << macToString(entry->mac) << "   "
       << ipToString(entry->ip) << "   "
       << std::chrono::duration_cast<seconds>((now - entry->timeAdded)).count() << " seconds   "
       << entry->isValid
       << "\n";
  }
  os << std::endl;
  return os;
}

} // namespace simple_router
