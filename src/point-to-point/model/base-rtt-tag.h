/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#ifndef BASE_RTT_TAG_H
#define BASE_RTT_TAG_H

#include "ns3/tag.h"

namespace ns3 {

class BaseRttTag : public Tag {
   public:
    BaseRttTag();

    static TypeId GetTypeId(void);
    virtual TypeId GetInstanceTypeId(void) const;
    virtual void Print(std::ostream &os) const;
    virtual uint32_t GetSerializedSize(void) const;
    virtual void Serialize(TagBuffer i) const;
    virtual void Deserialize(TagBuffer i);

    void SetBaseRttNs(uint64_t baseRttNs);
    uint64_t GetBaseRttNs() const;

   private:
    uint64_t m_baseRttNs;
};

}  // namespace ns3

#endif /* BASE_RTT_TAG_H */
