#include "ns3/rdma-queue-pair.h"
#include "ns3/rdma-hw.h"
#include "adamap-receiver.h"
#include "ns3/log.h"


namespace ns3 {

////////////////////////////////////////////////////////////////////////
// ReceiverAdamap
////////////////////////////////////////////////////////////////////////

NS_LOG_COMPONENT_DEFINE("ReceiverAdamap");
NS_OBJECT_ENSURE_REGISTERED(ReceiverAdamap);

TypeId
ReceiverAdamap::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::ReceiverAdamap")
    .SetParent<Object> ()
    .SetGroupName ("Tutorial")
    .AddConstructor<ReceiverAdamap> ()
    .AddAttribute("BitmapSize", "bitmap size",
              UintegerValue(32),
              MakeUintegerAccessor(&ReceiverAdamap::GetMapSize, &ReceiverAdamap::SetMapSize),
              MakeUintegerChecker<uint32_t>())
    ;
  return tid;
}

uint32_t ReceiverAdamap::GetMapSize () const {
    return m_bitmapSize;
}
void ReceiverAdamap::SetMapSize (uint32_t bitmapSize)
{
    m_reprLength = bitmapSize;
    m_bitmapSize = bitmapSize;
    m_bitmap.resize(bitmapSize, false);
}

ReceiverAdamap::ReceiverAdamap(uint32_t bitmapSize)
{
  first_n = 2;
  m_lookupTableLruSize = 1;
  m_bitmap.resize(bitmapSize, false);
  m_startSeq = 0;
  adamap_id = 0;
  m_currTableIndex = 0;
  m_reprLength = bitmapSize;
  last_table_index = -1;
  max_omni_type = 0;
  current_omni_type = 0;
  avg_omni_rtt = MicroSeconds(0);
  omni_scale_rto = MicroSeconds(0);
  table_timeout_delay = MicroSeconds(100);
  list_timeout_delay = MicroSeconds(100);
  rtt_scale_factor = 1.5;
  omni_rtt_cnt = 0;
  m_last_access_table_index = -1;
  m_last_access_table_max_retrans = -1;
  // 状态变量
  get_last_packet = false;
  isFinished = false;
  m_linkedListAccessCount = 0;
  m_linkedListCacheHitCount = 0;
  m_lookupTableAccessCount = 0;
  m_lookupTableCacheHitCount = 0;
  printf("%lu: Create ReceiverAdamap with bitmap size %u\n", Simulator::Now().GetTimeStep(), bitmapSize);
}

ReceiverAdamap::~ReceiverAdamap()
{
}


bool ReceiverAdamap::IsCurrentBitmapEmpty() const
{
  return std::all_of(m_bitmap.begin(), m_bitmap.end(), [](bool b){ return !b; });
}


void ReceiverAdamap::ResetCurrentBitmap(uint32_t newseq)
{
  std::fill(m_bitmap.begin(), m_bitmap.end(), false);
  m_reprLength = m_bitmap.size();
  m_startSeq = newseq;
}

uint32_t ReceiverAdamap::EstimateAdamapDmaBytes(const Adamap &adamap) const {
  uint32_t bitmapBytes = (adamap.bitmap.size() + 7) / 8;
  // metadata fields + bitmap payload (coarse model)
  return 32 + bitmapBytes;
}

void ReceiverAdamap::AddRnicDmaDelay(Time* delay, uint16_t opType, uint32_t bytes, bool isWrite) {
  if (m_hw == NULL) {
    return;
  }
  int32_t flowId = (m_RxQp == NULL) ? -1 : m_RxQp->m_flow_id;
  Time opDelay = m_hw->SubmitRnicDmaOp(opType, bytes, isWrite, flowId);
  if (delay != NULL) {
    *delay += opDelay;
  }
}

bool ReceiverAdamap::assertTableFinish() const {
  for (const auto& item : m_lookupTable) {
    if (!item.isFinish) {
        return false;
    }
  }
  return true;
}

bool ReceiverAdamap::IsFinishConditionSatisfied() const {
  return get_last_packet && m_finishedBitmaps.empty() && assertTableFinish();
}

bool ReceiverAdamap::assertFinish() {
  if (IsFinishConditionSatisfied()) {
    printf("%lu: Flow %u get all packets. Finishes.\n", Simulator::Now().GetTimeStep(), m_RxQp->m_flow_id);
    isFinished = true;
    return true;
  }
  return false;
}


// Adamap 表示的丢包的0全部收到了（包括bitmap和range）
bool ReceiverAdamap::assertLLNodeFinish(const Adamap &adamap) const {
  bool res = true;
  for (int i = 0; i < std::min((uint32_t)adamap.bitmap.size(), adamap.reprLength); i++) {
    res = res && adamap.bitmap[i];
  }
  if (adamap.reprLength <= adamap.bitmap.size()) {
    return res;
  } else {  // 有范围表示，就肯定没有完成
    return false;
  }
}

// Adamap 表示的丢包的0全部收到了（包括bitmap和range）
bool ReceiverAdamap::assertBitmapFull(const Adamap &adamap) const {
  bool res = true;
  for (int i = 0; i < std::min((uint32_t)adamap.bitmap.size(), adamap.reprLength); i++) {
    res = res && adamap.bitmap[i];
  }
  return res;
}

// 当前的index是bitmap的最后一个洞（此后再没有洞了）
bool ReceiverAdamap::assertLastHole(const Adamap &adamap, int index) const {
  bool res = true;
  for (int i = index + 1; i < std::min((uint32_t)adamap.bitmap.size(), adamap.reprLength); i++) {
    res = res && adamap.bitmap[i];
  }
  return res;
}

void ReceiverAdamap::UpdateOmniRto(Adamap_with_index& desiredElement) {
  Time rtt = Simulator::Now() - desiredElement.lastCallTime;
  avg_omni_rtt = MicroSeconds((avg_omni_rtt.GetMicroSeconds() * omni_rtt_cnt + rtt.GetMicroSeconds()) / (omni_rtt_cnt + 1));
  omni_scale_rto = MicroSeconds(avg_omni_rtt.GetMicroSeconds() * rtt_scale_factor);
  omni_rtt_cnt++;
  printf("%lu: Update avg_omni_rtt of flow %u to %lu us\n", Simulator::Now().GetTimeStep(), m_RxQp->m_flow_id, avg_omni_rtt.GetMicroSeconds());
}


int ReceiverAdamap::Record(uint32_t seq, uint16_t retrans_tier, Adamap_with_index& adamap_for_NACK, 
                          Adamap& adamap_for_print, int& new_high_tier_cnt, Time* delay, int32_t tableIndex)
{
  // NS_LOG_INFO("Record seq: " << seq);
// #ifdef OMNI_DETAIL
if (retrans_tier > 0)
  printf("%lu : [ReceiverAdamap::Record()] retrans tier: %u, seq: %u, table_index: %d\n", Simulator::Now().GetTimeStep(), retrans_tier, seq, tableIndex);
// #endif
  new_high_tier_cnt = 0;
  m_table_last_end = m_lookupTable.end();

  if (retrans_tier == 0) {        // 首次收到包
    // 如果当前bitmap中没有记录任何数据，且新序列号与当前起始连续，直接更新起始序列号，不需要记录bitmap
    if (IsCurrentBitmapEmpty() && (seq == m_startSeq + 1) || seq == 0) {
#ifdef OMNI_DETAIL
      printf("update StartSeq to %u\n", m_startSeq);
#endif
      m_startSeq = seq;
      return -1;
    }

    // 否则，需要记录该包。计算相对于bitmap起始的偏移量
    uint32_t offset = seq - m_startSeq - 1;
    if (offset < m_bitmap.size()) {   // 如果偏移量在当前bitmap范围内，则记录该包（置1）
      printf("%lu: seq %u, offset %u within bitmap starting from %u\n", Simulator::Now().GetTimeStep(), seq, offset, m_startSeq);
      m_bitmap[offset] = true;
      return -5;
    }
    else {      // 如果偏移量超出当前bitmap范围，则更新当前bitmap的表示长度为偏移量（表示在bitmap范围之外还有连续丢包）
      m_reprLength = offset;
      printf("%lu: seq %u, offset %u out of bitmap starting from %u, trigger update\n", Simulator::Now().GetTimeStep(), seq, m_reprLength, m_startSeq);

      // m_finishedBitmaps.emplace_back(Adamap{adamap_id, m_bitmap, m_startSeq, m_reprLength });
      m_finishedBitmaps.emplace_back(Adamap_with_index{Adamap{adamap_id, m_bitmap, m_startSeq, m_reprLength}, (int32_t)adamap_id, false, Simulator::Now(), 0});
      AddRnicDmaDelay(delay, RdmaHw::RNIC_DMA_LL_APPEND_WRITE,
                      EstimateAdamapDmaBytes(m_finishedBitmaps.back().adamap), true);
      
      adamap_for_NACK = m_finishedBitmaps.back(); // 顺手送回RdmaHw，这里送的是Adamap，但是为了方便，套壳Adamap_with_index
      adamap_for_print = Adamap{adamap_id, m_bitmap, m_startSeq, m_reprLength};      // 顺手送回RdmaHw
      PrintAdamap(&(adamap_for_NACK.adamap), "Receiver generate Adamap");
      printf("adamap_for_NACK's bitmap.size(): %lu\n", adamap_for_NACK.adamap.bitmap.size());

      // 初始化一个新的bitmap用于后续记录，并将起始序号更新为当前收到的包序号
      m_startSeq = seq;
      // printf("%lu: Initialize a new Adamap, Start Sequence = %u\n", Simulator::Now().GetTimeStep(), m_startSeq);
      ResetCurrentBitmap(seq);
      // return adamap_id++;
      adamap_id++;
      return -6;
    }
  }

  else if (retrans_tier == 1) {        // 首次重传包，需要在链表中查找
    int res = -4;
    assert (seq < m_startSeq + 1);
    
    int res_offset = -10;
    int LinkedlistStartId;
    int AdamapIdxInLL = FindSequenceInHeadBitmaps(seq, retrans_tier, LinkedlistStartId, delay);

    if (AdamapIdxInLL == -1 || AdamapIdxInLL == -2) {      // 如果linked list head出现了超时重传，那么本次传输的包直接丢弃，这部分包交给table处理。
      printf("FindSequence() return %d (%s). Maybe a multi-retransmission packet of linked list ?\n", AdamapIdxInLL, AdamapIdxInLL == -1 ? "not found" : "seq <= startSeq");
      return AdamapIdxInLL + res_offset;
    }
    else {  // cache hit or cache miss
      if (AdamapIdxInLL == -3) {      // cache miss
        printf("Cache miss: seq %u of retrans_tier %u not found in the first %d nodes. The node is actually at %dth.\n", seq, retrans_tier, first_n, LinkedlistStartId);
        if (!m_finishedBitmaps.empty()) {
          AddRnicDmaDelay(delay, RdmaHw::RNIC_DMA_LL_MISS_READ,
                          EstimateAdamapDmaBytes(m_finishedBitmaps.front().adamap), false);
        }
        AdamapIdxInLL = LinkedlistStartId;
      }
      // cache hit, or deal with cache miss as cache hit.
      printf("%lu: cache hit, Find target node %d. %s\n", Simulator::Now().GetTimeStep(), AdamapIdxInLL, AdamapIdxInLL > 0 ? "Deal with nodes before target node." : "");
      // step 1：处理链表中目标节点之前的节点
      for (int i = 0; i < AdamapIdxInLL; i++) {
        new_high_tier_cnt += PutLinkedListHeadToTable("1st retrans: ReceiverAdamap at previous node",
                                                      true, true, delay);
      }
      // step 2：处理目标节点
      Adamap_with_index& desiredElement = m_finishedBitmaps.front();
      // 验证确实在这个节点中
      PrintAdamap(&(desiredElement.adamap), "1st retrans: ReceiverAdamap at the exact node");
      assert (seq > desiredElement.adamap.startSeq && seq <= desiredElement.adamap.startSeq + desiredElement.adamap.reprLength);
      // printf("reprLength: %u, (a) desiredElement.adamap.bitmap.size(): %lu\n", desiredElement.adamap.reprLength, desiredElement.adamap.bitmap.size());
      // 如果在bitmap范围内
      if ((seq - desiredElement.adamap.startSeq) <= desiredElement.adamap.bitmap.size()) {
        assert (!desiredElement.adamap.bitmap[seq - desiredElement.adamap.startSeq - 1]);
        // printf("reprLength: %u, (b) desiredElement.adamap.bitmap.size(): %lu\n", desiredElement.adamap.reprLength, desiredElement.adamap.bitmap.size());
        
        if (desiredElement.max_retrans_omni_type == 0) {
          UpdateOmniRto(desiredElement);
          desiredElement.max_retrans_omni_type == 1;
        }

        desiredElement.adamap.bitmap[seq - desiredElement.adamap.startSeq - 1] = true;
        desiredElement.lastCallTime = Simulator::Now();
        desiredElement.max_retrans_omni_type = retrans_tier;
        printf("%lu: fill a bitmap hole at index %u.\n", Simulator::Now().GetTimeStep(), seq - desiredElement.adamap.startSeq - 1);
        res = -14;
        adamap_for_print = desiredElement.adamap;
        if (assertLastHole(desiredElement.adamap, seq - desiredElement.adamap.startSeq - 1)) {
          if (!assertBitmapFull(desiredElement.adamap)) {
            Adamap_with_index newAdamap;
            newAdamap.adamap = desiredElement.adamap;
            newAdamap.adamap.reprLength = std::min((uint32_t)desiredElement.adamap.bitmap.size(), desiredElement.adamap.reprLength);
            newAdamap.tableIndex = m_currTableIndex ++; // 新的Adamap的ID
            newAdamap.isFinish = false;
            newAdamap.lastCallTime = Simulator::Now();
            newAdamap.max_retrans_omni_type = retrans_tier;
            m_lookupTable.push_back(newAdamap);
            AddRnicDmaDelay(delay, RdmaHw::RNIC_DMA_LL_TO_TABLE_WRITE,
                            EstimateAdamapDmaBytes(newAdamap.adamap), true);
            max_omni_type = std::max(max_omni_type, newAdamap.max_retrans_omni_type);
            printf("(a) Add new Adamap to Lookup table. Table ID: %d\n", newAdamap.tableIndex);
            PrintAdamap(&(newAdamap.adamap), "gen LookUp Table Adamap");
            new_high_tier_cnt++;
            adamap_for_NACK = newAdamap;
            res = -15;
          }
          if (desiredElement.adamap.reprLength <= desiredElement.adamap.bitmap.size()) {
            // printf("reprLength: %u, (2) desiredElement.adamap.bitmap.size(): %lu\n", desiredElement.adamap.reprLength, desiredElement.adamap.bitmap.size());
            adamap_for_print = desiredElement.adamap;
            printf("%lu: Fullfill Adamap %d in linked list, erase.\n", Simulator::Now().GetTimeStep(), desiredElement.adamap.id);
            m_finishedBitmaps.erase(m_finishedBitmaps.begin());
            res = -16;
          } else {      // 上送后，向后滑窗
            // printf("desiredElement.reprLength = %u, desiredElement.bitmap.size() = %u", desiredElement.reprLength, desiredElement.bitmap.size());
            printf("Slide Adamap %d\n", desiredElement.adamap.id);
            desiredElement.adamap.reprLength -= desiredElement.adamap.bitmap.size();
            desiredElement.adamap.startSeq += desiredElement.adamap.bitmap.size();
            for (int i = 0; i < desiredElement.adamap.bitmap.size(); i++) {
              desiredElement.adamap.bitmap[i] = false;
            }
            // desiredElement.adamap.bitmap.assign(desiredElement.adamap.bitmap.size(), false);
            printf("size() of bitmap: %lu\n", desiredElement.adamap.bitmap.size());
            
            PrintAdamap(&(desiredElement.adamap), "Header Adamap after sliding");
            adamap_for_print = desiredElement.adamap;
            res = -17;
          }
        }
      }
      else {      // seq 在bitmap范围外，需要更新 bitmap 的表示长度
        // 有范围表示，就肯定没有完成
        uint32_t offset = seq - desiredElement.adamap.startSeq - 1;
        while (offset >= m_bitmap.size()) {   // 如果偏移量在当前bitmap范围内，则记录该包（置1）
          Adamap_with_index newAdamap;
          newAdamap.adamap = desiredElement.adamap;
          newAdamap.adamap.reprLength = std::min((uint32_t)desiredElement.adamap.bitmap.size(), desiredElement.adamap.reprLength);
          newAdamap.tableIndex = m_currTableIndex ++; // 新的Adamap的ID
          newAdamap.isFinish = false;
          newAdamap.lastCallTime = Simulator::Now();
          newAdamap.max_retrans_omni_type = retrans_tier;
          m_lookupTable.push_back(newAdamap);
          AddRnicDmaDelay(delay, RdmaHw::RNIC_DMA_LL_TO_TABLE_WRITE,
                          EstimateAdamapDmaBytes(newAdamap.adamap), true);
          max_omni_type = std::max(max_omni_type, newAdamap.max_retrans_omni_type);
          printf("(b) Add new Adamap to Lookup table. Table ID: %d\n", newAdamap.tableIndex);
          PrintAdamap(&(newAdamap.adamap), "LookUp Table Adamap");
          new_high_tier_cnt++;
          adamap_for_NACK = newAdamap;
          desiredElement.adamap.reprLength -= desiredElement.adamap.bitmap.size();
          desiredElement.adamap.startSeq += desiredElement.adamap.bitmap.size();
          desiredElement.adamap.bitmap.assign(desiredElement.adamap.bitmap.size(), false);     
          for (int i = 0; i < desiredElement.adamap.bitmap.size(); i++) {
            desiredElement.adamap.bitmap[i] = false;
          }     
          offset -= desiredElement.adamap.bitmap.size();
        }
        desiredElement.adamap.bitmap[offset] = true;
        adamap_for_print = desiredElement.adamap;
        res = -18;
      }
      
      // return AdamapIdxInLL;
    }
    for (int i = 0; i < new_high_tier_cnt; i++) {
      --m_table_last_end;
    }
    printf("%lu, Linked list length after a retrans_tier=1 packet: %lu\n", Simulator::Now().GetTimeStep(), m_finishedBitmaps.size());
    return res;
  }
  
  else { // (retrans_tier > 1) {    // 直接进行 table_index 的查询
    // 在m_lookupTable中查找tableIndex == index的节点
    for (auto it = m_lookupTable.begin(); it != m_lookupTable.end(); ++it) {
      if (it->tableIndex == tableIndex) {
        if(AccessLookupTableLru(tableIndex) == 1) {
          AddRnicDmaDelay(delay, RdmaHw::RNIC_DMA_TABLE_MISS_READ,
                          EstimateAdamapDmaBytes(it->adamap), false);
        }
        int trigger_resend = 0;
        // 更新该流的平均 rtt
        if (retrans_tier > it->max_retrans_omni_type) {
          UpdateOmniRto(*it);
        }
        Adamap& amap = it->adamap;
        // 断言 seq 在 (startSeq, startSeq + reprLength] 范围内
        assert(amap.startSeq < seq && seq <= amap.startSeq + amap.reprLength);
        
        // 计算 bitmap 内对应的索引
        uint32_t pos = seq - amap.startSeq - 1;
        // 如果 pos 在 bitmap 范围内，且该位为 0，则更新为 1
        
        if (pos < amap.bitmap.size()) {
          bool bit = amap.bitmap.at(pos);
          if (!bit) {
            amap.bitmap.at(pos) = true;
            // it->lastCallTime = Simulator::Now();         // 这里不更新，交给RdmaHw，重传 NACK 的时候再更新
            it->max_retrans_omni_type = retrans_tier;
          }
        }
        else {
          // 如果 pos 超出 bitmap 范围，或者该位已经是 1，则不需要更新
          printf("seq %u, pos %u out of bitmap starting from %u\n", seq, pos, amap.startSeq);
          printf("bitmap size: %lu, corresponding pos: %u\n", amap.bitmap.size(), static_cast<int>(amap.bitmap.at(pos)));
          PrintAdamap(&amap, "ReceiverAdamap::Record() - retrans_tier > 1");
          assert(pos < amap.bitmap.size() && !amap.bitmap.at(pos));
        }

        PrintAdamap(&amap, "After receiving a multi-retrans packet");
        // 检查从起始位置到 reprLength 表示的范围是否全部为 1
        bool allReceived = true;
        // 遍历 [0, reprLength) 范围
        for (uint32_t i = 0; i < amap.reprLength; i++) {
          if (i < amap.bitmap.size()) {
            if (!amap.bitmap[i]) {
              allReceived = false;
              break;
            }
          } else {
            // 对于超出 bitmap 长度的部分，视为未收到
            printf("tableIndex: %d, i: %u, amap.reprLength: %u, amap.bitmap.size(): %lu\n", tableIndex, i, amap.reprLength, amap.bitmap.size());
            PrintAdamap(&amap, "exceeds bitmap representation");
            assert(false && "item in Table should not have range representation");
            allReceived = false;
            break;
          }
        }
        
        // 如果全部为1，则将 isFinish 置为 true，并删除该节点
        if (allReceived) {
          it->isFinish = true;
          m_lookupTable.erase(it);  // This is OK, because when we erase, we also return. 
        }
        printf("%lu: Linked list length after processing retrans_tier > 1 packet: %lu\n", Simulator::Now().GetTimeStep(), m_finishedBitmaps.size());
        // 打印当前的lookup table
        printf("%lu: Lookup Table item after processing retrans_tier > 1 packet: ", Simulator::Now().GetTimeStep());
        for (auto tmp = m_lookupTable.begin(); tmp != m_lookupTable.end(); ++tmp) {
          printf("%d ", tmp->tableIndex);
        }
        printf("\n");
        // 更新最高table index
        last_table_index = tableIndex;
        //更新最高重传层次
        max_omni_type = std::max(max_omni_type, (int)retrans_tier);
        return -20 - trigger_resend; // 找到后退出循环
      }
    }
    printf("hahahahaha, seq=%u, retrans_tier=%u\n", seq, retrans_tier);
    return -100;
  }
}


int ReceiverAdamap::PutLinkedListHeadToTable(std::string str, bool do_erase, bool update_ts, Time* delay) {
  int newTableNodeCnt = 0;
  Adamap_with_index& desiredElement = m_finishedBitmaps.front();
  // PrintAdamap(&(desiredElement.adamap), str.c_str());
  if (!assertLLNodeFinish(desiredElement.adamap)) {  // 该节点中还有未收到的包
    bool transform_finish = 0;
    while (transform_finish == 0) {    // 拆分 Adamap 到 bitmap 表示
      Adamap firstPart;
      Adamap_with_index newAdamap;
      if (splitAdamap(desiredElement.adamap, firstPart, true)) {
        newAdamap.adamap = firstPart;
      } else {
        newAdamap.adamap = desiredElement.adamap;
        transform_finish = 1;
      }
      newAdamap.tableIndex = m_currTableIndex ++; // 新的Adamap的ID
      newAdamap.isFinish = false;
      if (update_ts) {
        newAdamap.lastCallTime = Simulator::Now();
      }
      newAdamap.max_retrans_omni_type = 1;
      m_lookupTable.push_back(newAdamap);
      AddRnicDmaDelay(delay, RdmaHw::RNIC_DMA_LL_TO_TABLE_WRITE,
                      EstimateAdamapDmaBytes(newAdamap.adamap), true);
      newTableNodeCnt ++;
      max_omni_type = std::max(max_omni_type, newAdamap.max_retrans_omni_type);
      printf("(c) Add new Adamap to Lookup table. Table ID: %d\n", newAdamap.tableIndex);
      PrintAdamap(&(newAdamap.adamap), "1st retrans: ReceiverAdamap at previous node. gen LookUp Table Adamap");
    }
  }
  if (do_erase)
    m_finishedBitmaps.erase(m_finishedBitmaps.begin());
  return newTableNodeCnt;
}


int ReceiverAdamap::AccessLookupTableLru(int32_t tableIndex) {
  ++m_lookupTableAccessCount;
  // 在 vector 中查找该元素
  int ret=0;
  auto it = std::find(m_lookupTableLru.begin(), m_lookupTableLru.end(), tableIndex);
  
  if (it != m_lookupTableLru.end()) {
    // 如果找到了，移动到头部
    m_lookupTableLru.erase(it);
  } else {
    ret = 1;
    // 如果没找到，并且已满，则删除最后一个元素
    if (m_lookupTableLru.size() >= m_lookupTableLruSize) {
      m_lookupTableLru.pop_back();
    }
  }
  // 插入新元素到头部
  m_lookupTableLru.insert(m_lookupTableLru.begin(), tableIndex);
  if (ret == 0) {
    ++m_lookupTableCacheHitCount;
  }
  return ret;
}


// 拆分函数：第一个参数为待拆解节点（拆分后其保存第二部分），
// 第二个参数为返回拆分出的第一部分。
// 返回 true 表示拆分成功，否则返回 false（即不需要拆分）。
// 这个函数保证第二部分始终是可用的，而且拆出的每一部分都不会是全1。
bool
ReceiverAdamap::splitAdamap(Adamap &node, Adamap &firstPart, bool neglectAllOneBitmap)
{
    size_t x = node.bitmap.size();

    // 如果不忽略全1的情况，则按原逻辑只拆分一次
    if (!neglectAllOneBitmap) {
        if (node.reprLength <= x) {
            return false;
        }
        firstPart.id = node.id;             // id不变
        firstPart.bitmap = node.bitmap;       // bitmap复制，大小不变
        firstPart.startSeq = node.startSeq;   // 起始序号不变
        firstPart.reprLength = static_cast<uint32_t>(x); // 第一部分长度为x

        // 更新 node 为第二部分
        node.startSeq += static_cast<uint32_t>(x);
        node.reprLength -= static_cast<uint32_t>(x);
        node.bitmap.assign(x, false);         // 重置为全0
        return true;
    }

    // 当 neglectAllOneBitmap 为 true 时，重复拆分直到拆出的第一部分的 bitmap 不全为1
    while (node.reprLength > x) {
        Adamap tempPart;
        tempPart.id = node.id;
        tempPart.bitmap = node.bitmap;      // 保持 bitmap 大小不变
        tempPart.startSeq = node.startSeq;
        tempPart.reprLength = static_cast<uint32_t>(x);

        // 更新 node 为第二部分
        node.startSeq += static_cast<uint32_t>(x);
        node.reprLength -= static_cast<uint32_t>(x);
        node.bitmap.assign(x, false);        // 重置为全0

        // 检查 tempPart.bitmap 是否全为1
        bool allOnes = true;
        for (bool bit : tempPart.bitmap) {
            if (!bit) {
                allOnes = false;
                break;
            }
        }
        // 如果拆出的部分不全为1，则返回该部分
        if (!allOnes) {
            firstPart = tempPart;
            return true;
        }
        // 否则，忽略本次拆分，继续对剩下的 node 进行拆分
    }
    // 如果循环结束，说明无法拆分出 bitmap 不全为1的部分
    return false;
}

int
ReceiverAdamap::FindSequenceInHeadBitmaps (uint32_t seq, uint16_t retrans_tier, int32_t& tableIndex, Time* delay)
{
  ++m_linkedListAccessCount;
  if (m_finishedBitmaps.empty ()){      // 链表为空，无法查找
    return -1; 
  }

  if (retrans_tier == 1) {
    uint32_t prefetchNodes = 0;
    uint32_t prefetchBytes = 0;
    for (auto itPref = m_finishedBitmaps.begin();
         itPref != m_finishedBitmaps.end() && prefetchNodes < static_cast<uint32_t>(first_n);
         ++itPref, ++prefetchNodes) {
      prefetchBytes += EstimateAdamapDmaBytes(itPref->adamap);
    }
    if (prefetchBytes > 0) {
      AddRnicDmaDelay(delay, RdmaHw::RNIC_DMA_LL_PREFETCH_READ, prefetchBytes, false);
    }
  }

  auto it = m_finishedBitmaps.begin ();
  if (seq <= it->adamap.startSeq) {           // (不应该返回这个值) 如果 seq 小于等于头部的起始序列号，返回 -2
    return -2;
  }
  tableIndex = m_finishedBitmaps.begin()->tableIndex;
  size_t i = 0; // 记录当前节点索引
  for (it = m_finishedBitmaps.begin(); it != m_finishedBitmaps.end(); ++i) {
    uint32_t start = it->adamap.startSeq;
    uint32_t end = start + it->adamap.reprLength;
    if (seq >= start && seq <= end) { // 如果 seq 在当前节点的范围内
      // m_finishedBitmaps.erase(m_finishedBitmaps.begin(), it);   // 找到目标序列号所在的节点，删除其之前的所有节点 (UPDATE: 不应该在这里删除！)
      if (i < first_n) {
        ++m_linkedListCacheHitCount;
        return i; // 返回找到的节点索引, cache hit
      }
      else {
        tableIndex = i;
        return -3; // 在前 n 个节点范围内都没找到, cache miss
      }
    }
    ++it;
  }
  return -1; // 未找到
}



std::string
ReceiverAdamap::PrintInternalState (void) const
{
  std::ostringstream oss;
  oss << "=== PacketDropper 内部状态 ===\n";
  oss << "当前活动的 Adamap:\n" << PrintCurrBitmap() << "\n\n";
  oss << "当前 m_startSeq = " << m_startSeq << ", m_reprLength = " << m_reprLength << "\n";
  oss << "当前 bitmap 向量: ";
  for (bool bit : m_bitmap)
  {
    oss << (bit ? "1" : "0");
  }
  oss << "\n\n已完成的 Adamap 记录:\n" << PrintAllFinishedBitmaps();
  return oss.str ();
}

Adamap ReceiverAdamap::GetCurrBitmap() const
{
  Adamap state;
  
  state.id = adamap_id;
  state.startSeq = m_startSeq;
  state.reprLength = m_reprLength;
  // 返回当前bitmap的拷贝
  state.bitmap = m_bitmap;
  return state;
}


std::string ReceiverAdamap::PrintCurrBitmap() const
{
  std::ostringstream oss;
  oss << "当前 Adamap " << adamap_id << "：起始序号 = " << m_startSeq
      << ", 表示长度 = " << m_reprLength << " (" << m_startSeq + 1 << " ~ " << m_startSeq + m_reprLength << "), bitmap = ";
  for (bool bit : m_bitmap)
  {
    oss << (bit ? "1" : "0");
  }
  return oss.str();
}


Adamap ReceiverAdamap::GetHeadBitmap() const
{
  if (m_finishedBitmaps.empty())
  {
    // 返回一个空状态：可将 startSeq 和 reprLength 设为0，bitmap为空
    return Adamap{999999999, std::vector<bool>(), 0, 0};
  }
  return m_finishedBitmaps.front().adamap;
}


std::string ReceiverAdamap::PrintHeadBitmap() const
{
  std::ostringstream oss;
  if (m_finishedBitmaps.empty())
  {
    oss << "无完成的 Adamap 记录。";
    return oss.str();
  }
  const Adamap &state = m_finishedBitmaps.front().adamap;
  oss << "链表头 Adamap " << state.id << "：起始序号 = " << state.startSeq
      << ", 表示长度 = " << state.reprLength << ", bitmap = ";
  for (bool bit : state.bitmap)
  {
    oss << (bit ? "1" : "0");
  }
  return oss.str();
}


Adamap ReceiverAdamap::GetTailBitmap () const
{
  if (m_finishedBitmaps.empty ())
  {
    return Adamap{999999999, std::vector<bool> (), 0, 0};
  }
  return m_finishedBitmaps.back().adamap;
}

// std::string ReceiverAdamap::PrintTailBitmap () const
// {
//   std::ostringstream oss;
//   if (m_finishedBitmaps.empty ())
//   {
//     oss << "无完成的 Adamap 记录。";
//     return oss.str ();
//   }
//   const Adamap &state = m_finishedBitmaps.back ().adamap;
//   oss << Simulator::Now().GetTimeStep() <<": 生成 Adamap " << state.id << "：起始序号 = " << state.startSeq
//       << ", 表示长度 = " << state.reprLength << " (" << m_startSeq + 1 << " ~ " << m_startSeq + m_reprLength << ") , bitmap = ";
//   for (bool bit : state.bitmap)
//   {
//     oss << (bit ? "1" : "0");
//   }
//   return oss.str ();
// }


void ReceiverAdamap::DeleteHeadBitmap()
{
  if (!m_finishedBitmaps.empty())
  {
    m_finishedBitmaps.pop_front();
  }
}


std::string
ReceiverAdamap::PrintAllFinishedBitmaps (void) const
{
  std::ostringstream oss;
  if (m_finishedBitmaps.empty ())
  {
    oss << "无完成的 Adamap 记录。";
    return oss.str ();
  }
  // 依次打印每个 finished bitmap，每个记录占一行
  for (const auto &state : m_finishedBitmaps)
  {
    oss << "Adamap " << state.adamap.id << "起始序号 = " << state.adamap.startSeq
        << ", 表示长度 = " << state.adamap.reprLength << ", bitmap = ";
    for (bool bit : state.adamap.bitmap)
    {
      oss << (bit ? "1" : "0");
    }
    oss << "\n";
  }
  return oss.str ();
}

} // namespace ns3
