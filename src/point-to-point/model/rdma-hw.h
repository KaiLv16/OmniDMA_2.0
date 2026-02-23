#ifndef RDMA_HW_H
#define RDMA_HW_H

#include <ns3/custom-header.h>
#include <ns3/node.h>
#include <ns3/rdma.h>
#include <ns3/selective-packet-queue.h>

#include <unordered_map>
#include <unordered_set>
#include <deque>

#include "qbb-net-device.h"
#include "rdma-queue-pair.h"

#include "ns3/adamap.h"
#include "ns3/adamap-sender.h"
#include "ns3/adamap-receiver.h"

namespace ns3 {

struct RdmaInterfaceMgr {
    Ptr<QbbNetDevice> dev;
    Ptr<RdmaQueuePairGroup> qpGrp;

    RdmaInterfaceMgr() : dev(NULL), qpGrp(NULL) {}
    RdmaInterfaceMgr(Ptr<QbbNetDevice> _dev) { dev = _dev; }
};

class RdmaHw : public Object {
   public:
    enum RnicDmaOpType {
        RNIC_DMA_LL_APPEND_WRITE = 1,
        RNIC_DMA_LL_TO_TABLE_WRITE = 2,
        RNIC_DMA_LL_PREFETCH_READ = 3,
        RNIC_DMA_LL_MISS_READ = 4,
        RNIC_DMA_TABLE_MISS_READ = 5,
    };
    enum OmniDmaEventType {
        OMNI_EVT_GEN_AND_CACHE = 0,
        OMNI_EVT_GEN_AND_UPLOAD = 1,
        OMNI_EVT_FETCH_LINKEDLIST = 2,
        OMNI_EVT_CONSUME_LINKEDLIST = 3,
        OMNI_EVT_ENTER_LOOKUPTABLE = 4,
        OMNI_EVT_FETCH_LOOKUPTABLE = 5,
        OMNI_EVT_CONSUME_LOOKUPTABLE = 6,
        OMNI_EVT_SENDER_GET_ADAMAP = 7,
        OMNI_EVT_FIRST_RETRANS_PROCESS = 8,
        OMNI_EVT_MULTI_RETRANS_PROCESS = 9,
    };

    struct RnicDmaStats {
        uint64_t submittedOps;
        uint64_t completedOps;
        uint64_t submittedReadOps;
        uint64_t submittedWriteOps;
        uint64_t submittedBytes;
        uint64_t completedBytes;
        uint64_t submittedReadBytes;
        uint64_t submittedWriteBytes;
        uint64_t totalQueueDelayNs;
        uint64_t totalServiceTimeNs;
        uint64_t maxQueueDelayNs;
        uint64_t maxQueueDepth;
        RnicDmaStats()
            : submittedOps(0),
              completedOps(0),
              submittedReadOps(0),
              submittedWriteOps(0),
              submittedBytes(0),
              completedBytes(0),
              submittedReadBytes(0),
              submittedWriteBytes(0),
              totalQueueDelayNs(0),
              totalServiceTimeNs(0),
              maxQueueDelayNs(0),
              maxQueueDepth(0) {}
    };

    static TypeId GetTypeId(void);
    RdmaHw();

    Ptr<Node> m_node;
    DataRate m_minRate;  //< Min sending rate
    uint32_t m_mtu;
    uint32_t m_cc_mode;
    double m_nack_interval;
    uint32_t m_chunk;
    uint32_t m_ack_interval;
    bool m_backto0;
    bool m_var_win, m_fast_react;
    bool m_rateBound;
    std::vector<RdmaInterfaceMgr> m_nic;  // list of running nic controlled by this RdmaHw
    std::unordered_map<uint64_t, Ptr<RdmaQueuePair>> m_qpMap;      // mapping from uint64_t to qp
    std::unordered_map<uint64_t, Ptr<RdmaRxQueuePair>> m_rxQpMap;  // mapping from uint64_t to rx qp
    std::unordered_map<uint32_t, std::vector<int>>
        m_rtTable;  // map from ip address (u32) to possible ECMP port (index of dev)

    // qp complete callback
    typedef Callback<void, Ptr<RdmaQueuePair>> QpCompleteCallback;
    QpCompleteCallback m_qpCompleteCallback;
    
    TracedCallback<int, int, uint32_t, uint32_t, const std::string &, const Adamap*> m_trace_omnidma_event;   // trace函数，用于记录omnidma的事件
    TracedCallback<int32_t, uint16_t, uint32_t, uint64_t, uint64_t, uint64_t, uint32_t>
        m_trace_rnic_dma_event;  // flowId, opType, bytes, qDelayNs, svcNs, backlogNs, qDepth

    void SetNode(Ptr<Node> node);
    void Setup(QpCompleteCallback cb);  // setup shared data and callbacks with the QbbNetDevice

    /* Akashic Record of finished QP */
    std::unordered_set<uint64_t> akashic_Qp;    // instance for each src
    std::unordered_set<uint64_t> akashic_RxQp;  // instance for each dst
    static uint64_t nAllPkts;                   // number of total packets

    /* TxQpeueuPair */
    static uint64_t GetQpKey(uint32_t dip, uint16_t sport, uint16_t dport,
                             uint16_t pg);          // get the lookup key for m_qpMap
    Ptr<RdmaQueuePair> GetQp(uint64_t key);         // get the qp
    uint32_t GetNicIdxOfQp(Ptr<RdmaQueuePair> qp);  // get the NIC index of the qp
    void DeleteQueuePair(Ptr<RdmaQueuePair> qp);    // delete TxQP

    void AddQueuePair(uint64_t size, uint16_t pg, Ipv4Address _sip, Ipv4Address _dip,
                      uint16_t _sport, uint16_t _dport, uint32_t win, uint64_t baseRtt,
                      int32_t flow_id, bool omniDMA_enable, uint16_t bitmap_size);  // add a nw qp (new send)
    void AddQueuePair(uint64_t size, uint16_t pg, Ipv4Address _sip, Ipv4Address _dip,
                      uint16_t _sport, uint16_t _dport, uint32_t win, uint64_t baseRtt) {
        this->AddQueuePair(size, pg, _sip, _dip, _sport, _dport, win, baseRtt, -1, false, 0);
    }

    /* RxQueuePair */
    static uint64_t GetRxQpKey(uint32_t dip, uint16_t dport, uint16_t sport, uint16_t pg);
    Ptr<RdmaRxQueuePair> GetRxQp(uint32_t sip, uint32_t dip, uint16_t sport, uint16_t dport,
                                 uint16_t pg, bool create,
                                 bool omniDMA_enable=0, uint16_t bitmap_size=0);  // get a rxQp
    uint32_t GetNicIdxOfRxQp(Ptr<RdmaRxQueuePair> q);        // get the NIC index of the rxQp
    void DeleteRxQp(uint32_t dip, uint16_t dport, uint16_t sport, uint16_t pg);  // delete RxQP

    int ReceiveUdp(Ptr<Packet> p, CustomHeader &ch);


    void TransmitOmniNACK(int tx_type, Ptr<RdmaRxQueuePair> rxQp, CustomHeader &ch, const Adamap_with_index &entry, Time delay=MicroSeconds(0), int omni_type=-1);

    int ReceiveCnp(Ptr<Packet> p, CustomHeader &ch);
    int ReceiveFastCnp(Ptr<Packet> p, CustomHeader &ch);
    int ReceiveAck(Ptr<Packet> p, CustomHeader &ch);  // handle both ACK and NACK
    int Receive(Ptr<Packet> p,
                CustomHeader &
                    ch);  // callback function that the QbbNetDevice should use when receive
                          // packets. Only NIC can call this function. And do not call this upon PFC

    void CheckandSendQCN(Ptr<RdmaRxQueuePair> q);
    int ReceiverCheckSeq(uint32_t seq, Ptr<RdmaRxQueuePair> q, uint32_t size, bool &cnp);
    void AddHeader(Ptr<Packet> p, uint16_t protocolNumber);
    static uint16_t EtherToPpp(uint16_t protocol);

    void RecoverQueue(Ptr<RdmaQueuePair> qp);
    void QpComplete(Ptr<RdmaQueuePair> qp);
    void SetLinkDown(Ptr<QbbNetDevice> dev);

    // call this function after the NIC is setup
    void AddTableEntry(Ipv4Address &dstAddr, uint32_t intf_idx);
    void ClearTable();
    void RedistributeQp();

    Ptr<Packet> GetNxtPacket(Ptr<RdmaQueuePair> qp);  // get next packet to send, inc snd_nxt
    void PktSent(Ptr<RdmaQueuePair> qp, Ptr<Packet> pkt, Time interframeGap);
    void UpdateNextAvail(Ptr<RdmaQueuePair> qp, Time interframeGap, uint32_t pkt_size);
    void ChangeRate(Ptr<RdmaQueuePair> qp, DataRate new_rate);

    void HandleTimeout(Ptr<RdmaQueuePair> qp, Time rto);

    void HandleOmniListTimeout(uint16_t omni_type, Ptr<RdmaRxQueuePair> rxQp, CustomHeader &ch);
    void HandleOmniTableTimeout(uint16_t omni_type, Ptr<RdmaRxQueuePair> rxQp, CustomHeader &ch);
    // void HandleOmniContextTimeout(uint16_t omni_type, Ptr<RdmaRxQueuePair> rxQp, CustomHeader &ch);

    /* statistics */
    uint32_t cnp_by_ecn;
    uint32_t cnp_by_ooo;
    uint32_t cnp_total;
    size_t getIrnBufferOverhead();  // get buffer overhead for IRN

    /******************************
     * Mellanox's version of DCQCN
     *****************************/
    double m_g;               // feedback weight
    double m_rateOnFirstCNP;  // the fraction of line rate to set on first CNP
    bool m_EcnClampTgtRate;
    double m_rpgTimeReset;
    double m_rateDecreaseInterval;
    uint32_t m_rpgThreshold;
    double m_alpha_resume_interval;
    DataRate m_rai;   //< Rate of additive increase
    DataRate m_rhai;  //< Rate of hyper-additive increase

    // the Mellanox's version of alpha update:
    // every fixed time slot, update alpha.
    void UpdateAlphaMlx(Ptr<RdmaQueuePair> q);
    void ScheduleUpdateAlphaMlx(Ptr<RdmaQueuePair> q);

    // Mellanox's version of CNP receive
    void cnp_received_mlx(Ptr<RdmaQueuePair> q);

    // Mellanox's version of rate decrease
    // It checks every m_rateDecreaseInterval if CNP arrived (m_decrease_cnp_arrived).
    // If so, decrease rate, and reset all rate increase related things
    void CheckRateDecreaseMlx(Ptr<RdmaQueuePair> q);
    void ScheduleDecreaseRateMlx(Ptr<RdmaQueuePair> q, uint32_t delta);

    // Mellanox's version of rate increase
    void RateIncEventTimerMlx(Ptr<RdmaQueuePair> q);
    void RateIncEventMlx(Ptr<RdmaQueuePair> q);
    void FastRecoveryMlx(Ptr<RdmaQueuePair> q);
    void ActiveIncreaseMlx(Ptr<RdmaQueuePair> q);
    void HyperIncreaseMlx(Ptr<RdmaQueuePair> q);

    // Implement Timeout according to IB Spec Vol. 1 C9-139.
    // For an HCA requester using Reliable Connection service, to detect missing responses,
    // every Send queue is required to implement a Transport Timer to time outstanding requests.
    Time m_waitAckTimeout;

    /***********************
     * High Precision CC
     ***********************/
    double m_targetUtil;
    double m_utilHigh;
    uint32_t m_miThresh;
    bool m_multipleRate;
    bool m_sampleFeedback;  // only react to feedback every RTT, or qlen > 0
    void HandleAckHp(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader &ch);
    void UpdateRateHp(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader &ch, bool fast_react);
    void UpdateRateHpTest(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader &ch, bool fast_react);
    void FastReactHp(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader &ch);

    /**********************
     * TIMELY
     *********************/
    double m_tmly_alpha, m_tmly_beta;
    uint64_t m_tmly_TLow, m_tmly_THigh, m_tmly_minRtt;
    void HandleAckTimely(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader &ch);
    void UpdateRateTimely(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader &ch, bool us);
    void FastReactTimely(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader &ch);

    /**********************
     * DCTCP
     *********************/
    DataRate m_dctcp_rai;
    void HandleAckDctcp(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader &ch);

    /**********************
     * IRN
     *********************/
    bool m_irn;             // 主文件中 rdmaHw->SetAttribute设置的
    Time m_irn_rtoLow;      // 主文件中 rdmaHw->SetAttribute设置的
    Time m_irn_rtoHigh;     // 主文件中 rdmaHw->SetAttribute设置的
    uint32_t m_irn_bdp;     // 在主文件中手动计算（topo2bdpMap）

    /**********************
     * OmniDMA
     *********************/
    bool m_omnidma;             // 主文件中 rdmaHw->SetAttribute设置的
    Time m_omnidmaTimeout;  // timeout for omnidma

    /**********************
     * RNIC DMA Scheduler (for OmniDMA metadata ops)
     *********************/
    bool m_rnicDmaSchedEnable;
    DataRate m_rnicDmaBw;
    Time m_rnicDmaFixedLatency;
    Time m_rnicDmaNextAvailable;
    RnicDmaStats m_rnicDmaStats;
    struct RnicDmaCompletionRecord {
        Time done;
        uint32_t bytes;
        RnicDmaCompletionRecord(Time d = Time(0), uint32_t b = 0) : done(d), bytes(b) {}
    };
    std::deque<RnicDmaCompletionRecord> m_rnicDmaCompletions;

    Time SubmitRnicDmaOp(uint16_t opType, uint32_t bytes, bool isWrite, int32_t flowId = -1);
    void RefreshRnicDmaSchedulerState();
    uint32_t GetRnicDmaInflightOps();
    uint64_t GetRnicDmaBacklogDelayNs();
    const RnicDmaStats &GetRnicDmaStats() const { return m_rnicDmaStats; }
};

} /* namespace ns3 */

#endif /* RDMA_HW_H */
