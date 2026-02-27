#ifndef ADAMAP_RECEIVER_H
#define ADAMAP_RECEIVER_H

#include "ns3/ptr.h"
#include "ns3/object.h"
#include "ns3/core-module.h"
#include <list>
#include <vector>
#include <stdint.h>
#include <string>
#include <sstream>
#include <cassert>
#include "adamap.h" // 引入 Adamap 结构体
#include <ns3/simulator.h>
#include <algorithm>
// #include "ns3/rdma-queue-pair.h"

/*
struct Adamap 
{
  uint32_t id;
  std::vector<bool> bitmap; ///< 保存bitmap内容（1表示收到）
  uint32_t startSeq;        ///< 该bitmap对应的起始序列号
  uint32_t reprLength;      ///< 当前bitmap表示的长度（可能大于bitmap长度，表示连续丢包）
};
*/

namespace ns3 {

  class RdmaRxQueuePair;  // 仅前向声明，不 include 头文件
  class RdmaHw;

class ReceiverAdamap : public Object
{
public:

  Ptr<RdmaRxQueuePair> m_RxQp;
  Ptr<RdmaHw> m_hw;
  
  EventId m_omni_list_retransmit;
  EventId m_omni_table_retransmit;

  static TypeId GetTypeId (void);

  ReceiverAdamap(uint32_t bitmapSize = kDefaultOmniDmaBitmapSize);
  virtual ~ReceiverAdamap();

  /**
   * \brief 记录收到的数据包序列号
   *
   * 数据包序列号一定是递增的。如果当前bitmap没有记录任何丢失且序列号连续，
   * 则直接更新起始序列号；否则，根据偏移量判断是否在bitmap范围内记录，或
   * 更新表示长度，并将当前bitmap保存到链表中，然后初始化一个新的bitmap。
   *
   * \param seq 数据包的序列号
   */
  int Record(uint32_t seq, uint16_t retrans_tier, Adamap_with_index& adamap_for_NACK, 
                 Adamap& adamap_for_print, int& new_high_tier_cnt, Time* delay=NULL, int32_t tableIndex = -1);

  // int RecordNormalPackets(uint32_t seq, Adamap_with_index* adamap_for_NACK, Adamap* adamap_for_print);

  // int RecordFirstRetransPackets(uint32_t seq, Adamap_with_index *adamap_for_NACK = NULL, 
  //                Adamap *adamap_for_print = NULL, int* new_high_tier_cnt = NULL, int32_t tableIndex = -1);

  void UpdateOmniRto(Adamap_with_index& desiredElement);
  int PutLinkedListHeadToTable(std::string = "Put LinkedList Head To Table", bool do_erase=true, bool update_ts=false, Time* delay=NULL); 
  bool splitAdamap(Adamap &node, Adamap &firstPart, bool neglectAllOneBitmap = false);
  
  // int LinkedlistRtoOmniNACK(Ptr<RdmaRxQueuePair> rxQp, CustomHeader &ch);
  // int LookuptableRtoOmniNACK(Ptr<RdmaRxQueuePair> rxQp, CustomHeader &ch);

  bool assertTableFinish() const;
  bool IsFinishConditionSatisfied() const;
  bool assertFinish();

  bool assertLLNodeFinish(const Adamap &adamap) const;  // 单个节点的结束
  bool assertBitmapFull(const Adamap &adamap) const;
  bool assertLastHole(const Adamap &adamap,int index) const;
  /**
   * \brief 在链表头部的 n 个 bitmap 内查找指定序列号
   * \param index 目标序列号
   * \param n 需要检查的链表头部节点数
   * \return 若找到，返回对应的节点索引；若 index 过小，返回 -2；
   *         若前 n 个节点中找不到，返回 -3。
   */
  int FindSequenceInHeadBitmaps (uint32_t seq, uint16_t retrans_tier, int32_t& tableIndex, Time* delay=NULL);


  uint32_t GetMapSize () const;
  void SetMapSize (uint32_t bitmapSize);
  uint32_t GetLookupTableLruSize() const;
  void SetLookupTableLruSize(uint32_t lruSize);
  uint32_t GetFirstN () const;
  void SetFirstN (uint32_t firstN);

  Adamap GetCurrBitmap() const;
  std::string PrintCurrBitmap() const;

  Adamap GetHeadBitmap() const;
  std::string PrintHeadBitmap() const;

  Adamap GetTailBitmap () const;
  // std::string PrintTailBitmap () const;

  std::string PrintAllFinishedBitmaps (void) const;
  std::string PrintInternalState (void) const;
  void DeleteHeadBitmap();

  int AccessLookupTableLru(int32_t tableIndex);

  uint64_t GetLinkedListAccessCount() const { return m_linkedListAccessCount; }
  uint64_t GetLinkedListCacheHitCount() const { return m_linkedListCacheHitCount; }
  uint64_t GetLookupTableAccessCount() const { return m_lookupTableAccessCount; }
  uint64_t GetLookupTableCacheHitCount() const { return m_lookupTableCacheHitCount; }

  int m_currTableIndex;
  std::list<Adamap_with_index> m_lookupTable; // reprLength 没有用，只用具体bitmap和startSeq。
  int m_lookupTableLruSize;
  std::vector<int32_t> m_lookupTableLru;

  int32_t last_table_index;
  int max_omni_type;
  int current_omni_type;
  Time avg_omni_rtt;
  Time omni_scale_rto;
  double rtt_scale_factor;

  Time last_scan_rto;
  int omni_rtt_cnt;

  Time table_timeout_delay;
  Time list_timeout_delay;
  
  // 用于触发积极重传
  uint32_t m_last_access_table_index;        ///< 上次收到的序列号
  int m_last_access_table_max_retrans; ///< 上次收到的序列号，用于 NACK


  std::list<Adamap_with_index>::iterator m_table_last_end;  // 记录上次添加前的末尾

  bool IsCurrentBitmapEmpty() const;
  void ResetCurrentBitmap(uint32_t newseq = 0);
  uint32_t EstimateAdamapDmaBytes(const Adamap &adamap) const;
  void AddRnicDmaDelay(Time* delay, uint16_t opType, uint32_t bytes, bool isWrite);
  void RefillLinkedListHeadCacheFromHost(Time* delay);
  void EraseLinkedListHeadAndRefill(Time* delay);
  void AppendFinishedBitmapToLinkedList(const Adamap &adamap, Time* delay);
  void AddLinkedListMissReadDelay(Time* delay, uint32_t targetIndex);

  // 状态变量
  bool get_last_packet;
  uint32_t first_n;
  int m_bitmapSize;

  bool isFinished;

  // RNIC cache hit statistics for Adamap metadata accesses.
  uint64_t m_linkedListAccessCount;
  uint64_t m_linkedListCacheHitCount;
  uint64_t m_lookupTableAccessCount;
  uint64_t m_lookupTableCacheHitCount;
  uint32_t m_llHeadCacheNodeCount;
  uint32_t m_llHostNodeCount;

  // 当前活动的bitmap及其相关变量
  std::vector<bool> m_bitmap; ///< 当前bitmap，初始化为全0
  uint32_t adamap_id;
  uint32_t m_startSeq;        ///< 当前bitmap的起始序列号
  uint32_t m_reprLength;      ///< 当前bitmap的“表示长度”，初始为 m_bitmap.size()

  // 通过链表保存已经记录完成的bitmap记录
  std::list<Adamap_with_index> m_finishedBitmaps;
};

} // namespace ns3

#endif /* ADAMAP_RECEIVER_H */
