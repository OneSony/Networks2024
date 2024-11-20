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

  // FILL THIS IN

  //TODO 调用函数remove
  //TODO 重发packet在router里面做？

  for(auto it = m_cacheEntries.begin(); it != m_cacheEntries.end();) {
    //在调用前已经更新了valid
    if((*it)->isValid == false) {
      it = m_cacheEntries.erase(it);
    }else{
      it++;
    }
  }

  for(auto it = m_arpRequests.begin(); it != m_arpRequests.end();) {
    //看看现在这个有没有插入到表格里面，router收到了之后会直接插入

    if(lookup((*it)->ip) == nullptr) {
      //如果没有插入到表格里面，那么就要继续发送ARP请求
      if((*it)->nTimesSent >= 5) {
        //如果发送了5次了，那么就要发送ICMP host unreachable
        for(auto packet_it = (*it)->packets.begin(); packet_it != (*it)->packets.end(); packet_it++) {
          //发送ICMP host unreachable
          Buffer packet = packet_it->packet;
          std::string iface = packet_it->iface;
          //TODO
          //m_router.sendPacket(packet, iface);
        }
        //删除这个请求
        it = m_arpRequests.erase(it);
      }else{
        //继续发送ARP请求

        auto iface = m_router.findIfaceByName((*it)->packets.front().iface); //TODO!!!!

        ethernet_hdr eth;
        memcpy(eth.ether_dhost, "\xff\xff\xff\xff\xff\xff", ETHER_ADDR_LEN);
        memcpy(eth.ether_shost, iface->addr.data(), ETHER_ADDR_LEN);

        eth.ether_type = ethertype_arp;

        arp_hdr arp_request;
        arp_request.arp_hrd = arp_hrd_ethernet;      // Ethernet
        arp_request.arp_pro = 0x0800; // IPv4
        arp_request.arp_hln = ETHER_ADDR_LEN;              // MAC 地址长度
        arp_request.arp_pln = 4;              // IPv4 地址长度
        arp_request.arp_op = arp_op_request;
        memcpy(arp_request.arp_sha, iface->addr.data(), ETHER_ADDR_LEN);
        arp_request.arp_sip = iface->ip;
        memcpy(arp_request.arp_tha, "\xff\xff\xff\xff\xff\xff", ETHER_ADDR_LEN);
        arp_request.arp_tip = (*it)->ip;

        Buffer packet_request;
        packet_request.insert(packet_request.end(), (unsigned char*)&eth, (unsigned char*)&eth + sizeof(ethernet_hdr));
        packet_request.insert(packet_request.end(), (unsigned char*)&arp_request, (unsigned char*)&arp_request + sizeof(arp_hdr));

        m_router.sendPacket(packet_request, iface->name);

        (*it)->timeSent = steady_clock::now();
        (*it)->nTimesSent++;
        it++;
      }
    }else{
      //如果插入到表格里面了，那么就要发送所有的packet
      for(auto packet_it = (*it)->packets.begin(); packet_it != (*it)->packets.end(); packet_it++) {
        //发送packet
        Buffer packet = packet_it->packet;
        std::string iface = packet_it->iface;
        m_router.sendPacket(packet, iface);
      }
      it = m_arpRequests.erase(it);
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
