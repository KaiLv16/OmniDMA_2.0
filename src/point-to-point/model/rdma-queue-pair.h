#ifndef RDMA_QUEUE_PAIR_H
#define RDMA_QUEUE_PAIR_H


#include <ns3/simulator.h>   // 用于添加与 Simulator::now()相关的依赖
#include <ns3/custom-header.h>
#include <ns3/data-rate.h>
#include <ns3/event-id.h>
#include <ns3/int-header.h>
#include <ns3/ipv4-address.h>
#include <ns3/object.h>
#include <ns3/packet.h>
#include <ns3/selective-packet-queue.h>

#include <ns3/adamap.h>
#include <ns3/adamap-sender.h>
#include "omni-rdma-cubic.h"

#include <climits> /* for CHAR_BIT */
#include <vector>
#include <cstdint>
#include <algorithm>
#include "ns3/timer.h"

#define BITMASK(b) (1 << ((b) % CHAR_BIT))
#define BITSLOT(b) ((b) / CHAR_BIT)
#define BITSET(a, b) ((a)[BITSLOT(b)] |= BITMASK(b))
#define BITCLEAR(a, b) ((a)[BITSLOT(b)] &= ~BITMASK(b))
#define BITTEST(a, b) ((a)[BITSLOT(b)] & BITMASK(b))
#define BITNSLOTS(nb) ((nb + CHAR_BIT - 1) / CHAR_BIT)

#define ESTIMATED_MAX_FLOW_PER_HOST 9120

namespace ns3 {

class RdmaHw;
class ReceiverAdamap;
enum CcMode {
    CC_MODE_DCQCN = 1,
    CC_MODE_HPCC = 3,
    CC_MODE_TIMELY = 7,
    CC_MODE_DCTCP = 8,
    CC_MODE_UNDEFINED = 0,
};

// 维护乱序列表，但是乱序窗口大小不是在这里。
class IrnSackManager {
   private:
    std::list<std::pair<uint32_t, uint32_t>> m_data;   // 序列号 + 长度

   public:
    int socketId{-1};

    IrnSackManager();
    IrnSackManager(int flow_id);
    void sack(uint32_t seq, uint32_t size);  // put blocks
    size_t discardUpTo(uint32_t seq);        // return number of blocks removed
    bool IsEmpty();
    bool blockExists(uint32_t seq, uint32_t size);  // query if block exists inside SACK table
    bool peekFrontBlock(uint32_t *pseq, uint32_t *psize);
    size_t getSackBufferOverhead();  // get buffer overhead

    friend std::ostream &operator<<(std::ostream &os, const IrnSackManager &im);

};

// class bitmapManager {
// public:
//     // 可配置长度的bitmap，每一位对应一个数据包
//     std::vector<bool> bitmap;

//     // 序列号区间和最高接收序列号（均为uint64）
//     uint64_t lowerBound;   // bitmap的基地址（最小序列号）
//     uint64_t upperBound;   // 最大序列号（下界 + bitmap.size() - 1）
//     uint64_t highestSeq;   // 当前接收到的最高序列号

//     // 用于延时触发NACK的timer（NS3定时器）
//     ns3::Timer nackTimer;

//     // 用于检查bitmap中为0的空洞的指针（这里简单用一个索引指针示例）
//     uint32_t holeChecker;

//     // 构造函数：设置初始的序列号区间以及bitmap的长度
//     ReceiverModule(uint64_t lb, uint32_t bitmapLength)
//         : lowerBound(lb),
//           upperBound(lb + bitmapLength - 1),
//           highestSeq(lb),
//           holeChecker(0),
//           bitmap(bitmapLength, false)
//     { }

//     ~ReceiverModule() { }

//     // 方法1：checkUDP(seq)
//     // 如果seq小于lowerBound返回-1，大于upperBound返回-2，
//     // 否则利用 (seq - lowerBound) 得到bitmap中的索引，检查该位是否为0，
//     // 若为0则置1并更新最高接收序列号（如果必要），返回1（代表可以返回ACK）
//     int checkUDP(uint64_t seq) {
//         if (seq < lowerBound) {
//             return -1;
//         }
//         uint32_t index = seq - lowerBound;
//         if (index >= bitmap.size()) {
//             return -2;
//         }
//         if (!bitmap[index]) {
//             bitmap[index] = true;
//             if (seq > highestSeq) {
//                 highestSeq = seq;
//             }
//             return 1; // 新包，返回ACK的状态码
//         }
//         // 已经收到过此包（重复包）
//         return 0;
//     }

//     // 从bitmap最低位开始，统计连续为1的个数，若大于0，则用该计数更新lowerBound，
//     // 同时更新upperBound（保证bitmap的长度不变），并调整bitmap窗口：
//     // 删除前面连续的1，同时在尾部补上相同数量的0
//     int updateLowerBound() {
//         uint32_t count = 0;
//         for (uint32_t i = 0; i < bitmap.size(); ++i) {
//             if (bitmap[i]) {
//                 count++;
//             } else {
//                 break;
//             }
//         }
//         if (count > 0) {
//             lowerBound += count;
//             // 移除bitmap前count个元素，并在末尾插入count个false
//             bitmap.erase(bitmap.begin(), bitmap.begin() + count);
//             bitmap.insert(bitmap.end(), count, false);
//             holeChecker = 0; // 重置空洞检查指针
//         }
//         return count;   // 返回新增的窗口大小
//     }

//     // 从bitmap的第index位开始向后逐位检查（检查范围不超过 highestSeq 对应的位）
//     // 如果遇到为0的位，返回该索引；若后续位均为1，则返回-1
//     int checkHole(size_t index) {
//         // 计算最高接收序列号对应的bitmap索引范围
//         uint32_t limit = highestSeq - lowerBound + 1;
//         if (limit > bitmap.size()) {
//             limit = bitmap.size();
//         }
//         for (uint32_t i = index; i < limit; ++i) {
//             if (!bitmap[i]) {
//                 return i;
//             }
//         }
//         return -1;
//     }

//     // 额外函数：启动NACK定时器，指定延时时间
//     void StartNackTimer(ns3::Time delay) {
//         nackTimer.Schedule(delay);
//     }

//     // 额外函数：取消NACK定时器
//     void StopNackTimer() {
//         nackTimer.Cancel();
//     }

//     // 额外函数：检查bitmap中空洞，若发现空洞可触发NACK（这里只做简单遍历示例）
//     void CheckAndSendNack() {
//         for (uint32_t i = 0; i < bitmap.size(); ++i) {
//             if (!bitmap[i]) {
//                 uint64_t missingSeq = lowerBound + i;
//                 // 这里应调用实际的NACK发送函数（伪代码）
//                 // SendNack(missingSeq);
//                 break;
//             }
//         }
//     }
// };


class RdmaQueuePair : public Object {
   public:
    Time startTime;
    Ipv4Address sip, dip;
    uint16_t sport, dport;
    uint64_t m_size;
    uint64_t snd_nxt, snd_una;  // next seq to send, the highest unacked seq
    uint16_t m_pg;
    uint16_t m_ipid;
    uint32_t m_win;       // bound of on-the-fly packets
    uint64_t m_baseRtt;   // base RTT of this qp
    DataRate m_max_rate;  // max rate
    bool m_var_win;       // variable window size
    Time m_nextAvail;     //< Soonest time of next send
    uint32_t wp;          // current window of packets
    uint32_t lastPktSize;
    int32_t m_flow_id;
    Time m_timeout;

    /******************************
     * runtime states
     *****************************/
    DataRate m_rate;  //< Current rate
    struct {
        DataRate m_targetRate;  //< Target rate
        EventId m_eventUpdateAlpha;
        double m_alpha;
        bool m_alpha_cnp_arrived;  // indicate if CNP arrived in the last slot
        bool m_first_cnp;          // indicate if the current CNP is the first CNP
        EventId m_eventDecreaseRate;
        bool m_decrease_cnp_arrived;  // indicate if CNP arrived in the last slot
        uint32_t m_rpTimeStage;
        EventId m_rpTimer;
    } mlx;
    struct {
        uint32_t m_lastUpdateSeq;
        DataRate m_curRate;
        IntHop hop[IntHeader::maxHop];
        uint32_t keep[IntHeader::maxHop];
        uint32_t m_incStage;
        double m_lastGap;
        double u;
        struct {
            double u;
            DataRate Rc;
            uint32_t incStage;
        } hopState[IntHeader::maxHop];
    } hp;
    struct {
        uint32_t m_lastUpdateSeq;
        DataRate m_curRate;
        uint32_t m_incStage;
        uint64_t lastRtt;
        double rttDiff;
    } tmly;
    struct {
        uint32_t m_lastUpdateSeq;
        uint32_t m_caState;
        uint32_t m_highSeq;  // when to exit cwr
        double m_alpha;
        uint32_t m_ecnCnt;
        uint32_t m_batchSizeOfAlpha;
    } dctcp;

    struct {
        bool m_enabled;
        uint32_t m_bdp;          // m_irn_maxAck_
        uint32_t m_highest_ack;  // m_irn_maxAck_
        uint32_t m_max_seq;      // m_irn_maxSeq_
        Time m_rtoLow;
        Time m_rtoHigh;
        IrnSackManager m_sack;
        bool m_recovery;
        uint32_t m_recovery_seq;
    } irn;

    struct {
        bool m_omnidma_enabled;
        uint16_t m_omnidma_bitmap_size;
        uint64_t m_maxNormalAckSeq{0};  // sender-observed max seq from normal ACKs (0xFC)
        SenderAdamap adamap_sender;
    } omniDMA;

    Ptr<OmniRdmaCubic> m_omniCubic;  // Optional OmniDMA CUBIC window controller
    
    struct {
        uint64_t txFirstTransPkts{0};
        uint64_t txFirstTransBytes{0};
        uint64_t txReTransPkts{0};
        uint64_t txReTransBytes{0};
    } stat;

    // Implement Timeout according to IB Spec Vol. 1 C9-139.
    // For an HCA requester using Reliable Connection service, to detect missing responses,
    // every Send queue is required to implement a Transport Timer to time outstanding requests.
    EventId m_retransmit;

    /***********
     * methods
     **********/
    static TypeId GetTypeId(void);
    RdmaQueuePair(uint16_t pg, Ipv4Address _sip, Ipv4Address _dip, uint16_t _sport,
                  uint16_t _dport);
    void SetSize(uint64_t size);
    void SetWin(uint32_t win);
    void SetBaseRtt(uint64_t baseRtt);
    void SetVarWin(bool v);
    void SetFlowId(int32_t v);
    void SetTimeout(Time v);

    void SetOmniDMAEnable(bool v);
    void SetOmniDMABitmapSize(uint16_t v);

    uint64_t GetBytesLeft();
    uint32_t GetHash(void);
    void Acknowledge(uint64_t ack);
    uint64_t GetOnTheFly();
    bool IsWinBound();
    uint64_t GetWin();  // window size calculated from m_rate
    bool IsFinished();
    inline bool IsFinishedConst() const { return snd_una >= m_size; }

    uint64_t HpGetCurWin();  // window size calculated from hp.m_curRate, used by HPCC

    // max_seq - irn.m_highest_ack。需要看一下highest_ack是选择性ACK吗，还是有中断也可以？看上去是不考虑中断的
    inline uint32_t GetIrnBytesInFlight() const {
        // IRN do not consider SACKed segments for simplicity
#if (SLB_DEBUG == true)
        std::cout << "-> call GetIrnBytesInFlight()   irn.m_max_seq: " << irn.m_max_seq << " irn.m_highest_ack: " <<  irn.m_highest_ack << std::endl;
# endif
        return irn.m_max_seq - irn.m_highest_ack;
    }

    Time GetRto(uint32_t mtu) {
#if (SLB_DEBUG == true)
        std::cout << Simulator::Now() << " Flow " << m_flow_id << ": Calling GetRto(), ";
# endif
        if (irn.m_enabled) {
            if (GetIrnBytesInFlight() >= 3 * mtu) {
#if (LLM_DEBUG == true)
                std::cout << Simulator::Now() << " Flow " << m_flow_id << " call GetRto(), return irn.m_rtoHigh = " << irn.m_rtoHigh <<std::endl;
# endif
                return irn.m_rtoHigh;
            }
#if (LLM_DEBUG == true)
            std::cout << Simulator::Now() << " Flow " << m_flow_id << " call GetRto(), return irn.m_rtoLow = " << irn.m_rtoLow <<std::endl;
# endif
            return irn.m_rtoLow;
        } else {
            // std::cout << "return m_timeout = " << m_timeout <<std::endl;
            return m_timeout;
        }
    }
    // 滑动窗口逻辑
    inline bool CanIrnTransmit(uint32_t mtu) const {
# if (LLM_DEBUG == true)
        std::cout << Simulator::Now() << " Flow " << m_flow_id << ": Calling CanIrnTransmit(), ";
# endif
        uint64_t len_left = m_size >= snd_nxt ? m_size - snd_nxt : 0;
        return !irn.m_enabled ||        // 不是IRN（不归IRN管）
               (GetIrnBytesInFlight() + ((len_left > mtu) ? mtu : len_left)) < irn.m_bdp ||  // 在途数据包小于BDP
               (snd_nxt < irn.m_highest_ack + irn.m_bdp);       // 在途数据包不能太小（小于BDP）？
    }
};

class RdmaRxQueuePair : public Object {  // Rx side queue pair
   public:
    struct ECNAccount {
        uint16_t qIndex;
        uint8_t ecnbits;
        uint16_t qfb;
        uint16_t total;

        ECNAccount() { memset(this, 0, sizeof(ECNAccount)); }
    };

    Ptr<RdmaHw> m_hw;
    
    ECNAccount m_ecn_source;
    uint32_t sip, dip;
    uint16_t sport, dport;
    uint16_t m_ipid;
    uint32_t ReceiverNextExpectedSeq;
    Time m_nackTimer;
    Time basertt;
    int32_t m_milestone_rx;
    uint32_t m_lastNACK;
    EventId QcnTimerEvent;  // if destroy this rxQp, remember to cancel this timer
    IrnSackManager m_irn_sack_;
    int32_t m_flow_id;

    bool m_omnidma_enabled;
    uint16_t m_omnidma_bitmap_size;
    bool omni_last_packet;
    uint32_t omni_cumulative_ack_seq;
    Ptr<ReceiverAdamap> adamap_receiver;

    static TypeId GetTypeId(void);
    RdmaRxQueuePair();
    uint32_t GetHash(void);
};

class RdmaQueuePairGroup : public Object {
   public:
    std::vector<Ptr<RdmaQueuePair>> m_qps;
    // std::vector<Ptr<RdmaRxQueuePair> > m_rxQps;
    char m_qp_finished[BITNSLOTS(ESTIMATED_MAX_FLOW_PER_HOST)];

    static TypeId GetTypeId(void);
    RdmaQueuePairGroup(void);
    uint32_t GetN(void);
    Ptr<RdmaQueuePair> Get(uint32_t idx);
    Ptr<RdmaQueuePair> operator[](uint32_t idx);
    void AddQp(Ptr<RdmaQueuePair> qp);
    // void AddRxQp(Ptr<RdmaRxQueuePair> rxQp);
    void Clear(void);
    inline bool IsQpFinished(uint32_t idx) {
        if (__glibc_unlikely(idx >= ESTIMATED_MAX_FLOW_PER_HOST)) return false;
        return BITTEST(m_qp_finished, idx);
    }

    inline void SetQpFinished(uint32_t idx) {
        if (__glibc_unlikely(idx >= ESTIMATED_MAX_FLOW_PER_HOST)) return;
        BITSET(m_qp_finished, idx);
    }
};

}  // namespace ns3

#endif /* RDMA_QUEUE_PAIR_H */
