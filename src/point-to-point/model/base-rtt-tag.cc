/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "base-rtt-tag.h"

namespace ns3 {

BaseRttTag::BaseRttTag() : m_baseRttNs(0) {}

TypeId BaseRttTag::GetTypeId(void) {
    static TypeId tid = TypeId("ns3::BaseRttTag").SetParent<Tag>().AddConstructor<BaseRttTag>();
    return tid;
}

TypeId BaseRttTag::GetInstanceTypeId(void) const { return GetTypeId(); }

void BaseRttTag::Print(std::ostream &os) const { os << "baseRttNs=" << m_baseRttNs; }

uint32_t BaseRttTag::GetSerializedSize(void) const { return sizeof(m_baseRttNs); }

void BaseRttTag::Serialize(TagBuffer i) const { i.WriteU64(m_baseRttNs); }

void BaseRttTag::Deserialize(TagBuffer i) { m_baseRttNs = i.ReadU64(); }

void BaseRttTag::SetBaseRttNs(uint64_t baseRttNs) { m_baseRttNs = baseRttNs; }

uint64_t BaseRttTag::GetBaseRttNs() const { return m_baseRttNs; }

}  // namespace ns3
