#include "ns3/rdma-queue-pair.h"
#include "adamap-sender.h"
#include "ns3/log.h"


namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("SenderAdamap");

TypeId
SenderAdamap::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::SenderAdamap")
    .SetParent<Object> ()
    .SetGroupName ("Network")
    .AddConstructor<SenderAdamap> ();
  return tid;
}

SenderAdamap::SenderAdamap ()
{
  NS_LOG_INFO ("SenderAdamap created.");
}

SenderAdamap::~SenderAdamap ()
{
  NS_LOG_INFO ("SenderAdamap destroyed.");
}

void
SenderAdamap::Enqueue (const Adamap &adamap, uint16_t omni_type, uint32_t table_index)
{
  m_adamapQueue.push_back (adamap);
  // NS_LOG_INFO ("[SenderAdamap] Enqueued Adamap ID: " << adamap.id);
  printf ("%lu : [SenderAdamap::Enqueue] Enqueued Adamap ID: %u, type=%u\n", 
                      Simulator::Now().GetTimeStep(), adamap.id, omni_type);

  // 遍历 Adamap 的序列号范围
  for (uint32_t seq = adamap.startSeq + 1; seq <= adamap.startSeq + adamap.reprLength; ++seq)
  {
    bool isLost = true; // 默认认为丢失

    if (seq - adamap.startSeq <= adamap.bitmap.size())
    {
      // 如果序列号在 bitmap 范围内，则检查 bitmap
      uint32_t bitmapIndex = seq - adamap.startSeq - 1;
      if (adamap.bitmap[bitmapIndex]) 
      {
        isLost = false;
      }
    }

    if (isLost)
    {
      // 将丢失的序列号存入 lossSeqQueue
      LossSeqEntry entry = {adamap.id, seq, omni_type, table_index};
      lossSeqQueue.push_back(entry);
      NS_LOG_INFO ("[SenderAdamap] Loss detected: ID=" << adamap.id << ", Seq=" << seq);
      printf ("%lu : [SenderAdamap::Enqueue] Enqueue Lost Packet: Adamap ID=%u, Seq=%u\n", 
                      Simulator::Now().GetTimeStep(), adamap.id, seq);
    }
  }
}

uint32_t
SenderAdamap::GetQueueSize () const
{
  return m_adamapQueue.size ();
}

Adamap
SenderAdamap::GetHeadAdamap () const
{
  if (m_adamapQueue.empty ())
  {
    NS_LOG_WARN ("[SenderAdamap] Queue is empty.");
    return Adamap();
  }
  return m_adamapQueue.front ();
}

void
SenderAdamap::PrintHeadAdamap () const
{
  if (m_adamapQueue.empty ())
  {
    NS_LOG_WARN ("[SenderAdamap] Queue is empty.");
    return;
  }

  const Adamap &head = m_adamapQueue.front ();
  NS_LOG_INFO ("[SenderAdamap] Head Adamap -> ID: " << head.id
              << ", StartSeq: " << head.startSeq
              << ", ReprLength: " << head.reprLength);
}

Adamap
SenderAdamap::GetAdamap (uint32_t index) const
{
  if (index >= m_adamapQueue.size ())
  {
    NS_LOG_WARN ("[SenderAdamap] Index out of range.");
    return Adamap();
  }
  return m_adamapQueue[index];
}

int
SenderAdamap::NumRetransPkts () const
{
  return lossSeqQueue.size ();
}

uint32_t
SenderAdamap::GetRetransSeq (int* id, uint16_t* omniType, uint32_t* table_index, bool update)
{
  if (lossSeqQueue.empty ())
  {
    NS_LOG_WARN ("[SenderAdamap] No lost packets to retransmit.");
    return 0;
  }

  const LossSeqEntry &entry = lossSeqQueue.front ();
  *id = entry.id;
  *omniType = entry.omniType;
  *table_index = entry.tableIndex;
  
  uint32_t lostSeq = entry.lossSeq;

  if (update)
  {
    lossSeqQueue.pop_front ();
  }

  return lostSeq;
}

} // namespace ns3
