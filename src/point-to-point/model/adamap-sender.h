#ifndef SENDER_ADAMAP_H
#define SENDER_ADAMAP_H

#include "ns3/ptr.h"
#include <deque>
#include "ns3/object.h"
#include "adamap.h" // 引入 Adamap 结构体
#include <ns3/simulator.h>
// #include "ns3/rdma-queue-pair.h"

namespace ns3 {

  class RdmaQueuePair;  // 仅前向声明，不 include 头文件

class SenderAdamap : public Object
{
public:
  Ptr<RdmaQueuePair> m_Qp;

  static TypeId GetTypeId ();
  SenderAdamap ();
  virtual ~SenderAdamap ();

  void Enqueue (const Adamap &adamap, uint16_t omni_type, uint32_t table_index=-1);
  uint32_t GetQueueSize () const;
  Adamap GetHeadAdamap () const;
  void PrintHeadAdamap () const;
  Adamap GetAdamap (uint32_t index) const;

  // 新增的方法
  int NumRetransPkts () const;
  uint32_t GetRetransSeq (int* id, uint16_t* omniType, uint32_t* table_index, bool update = true);

private:
  struct LossSeqEntry
  {
    uint32_t id;       ///< Adamap ID
    uint32_t lossSeq;  ///< 丢失的序列号
    uint16_t omniType; ///< 丢失的序列号对应的 omniType
    uint32_t tableIndex; ///< 丢失的序列号对应的 TableIndex
  };

  std::deque<Adamap> m_adamapQueue;       ///< 存储收到的 Adamap
  std::deque<LossSeqEntry> lossSeqQueue;  ///< 存储丢失的序列号

};

} // namespace ns3

#endif // SENDER_ADAMAP_H
