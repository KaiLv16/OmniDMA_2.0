#ifndef OMNI_RDMA_CUBIC_H
#define OMNI_RDMA_CUBIC_H

#include "ns3/nstime.h"
#include "ns3/object.h"
#include "ns3/ptr.h"

#include <cstdint>

namespace ns3
{

class RdmaQueuePair;

/**
 * Lightweight CUBIC controller for OmniDMA sender-side window control.
 *
 * This class is intentionally decoupled from the legacy RDMA CC paths in RdmaHw.
 * It updates qp->m_win (bytes) based on ACK/loss feedback and leaves qp->m_rate
 * unchanged so the existing rate scheduler can keep working.
 */
class OmniRdmaCubic : public Object
{
  public:
    static TypeId GetTypeId(void);

    OmniRdmaCubic() = default;

    void Initialize(Ptr<RdmaQueuePair> qp, uint32_t segmentSize, uint32_t initialWindowBytes);

    void OnAck(Ptr<RdmaQueuePair> qp,
               uint32_t ackedBytes,
               uint64_t ackSeq,
               uint32_t priorInFlight,
               const Time& rtt,
               bool lossSignal);

    void OnTimeout(Ptr<RdmaQueuePair> qp, uint64_t ackSeq, uint32_t bytesInFlight);

    bool IsInitialized() const;
    uint32_t GetCwndBytes() const;
    uint32_t GetSsThreshBytes() const;

  private:
    void ApplyWindow(Ptr<RdmaQueuePair> qp) const;
    void PktsAcked(const Time& rtt);
    void EnterLoss(uint64_t ackSeq, uint32_t bytesInFlight);
    uint32_t UpdateCnt(uint32_t segmentsAcked);

    bool m_initialized{false};
    uint32_t m_segmentSize{1000};

    // Tunables (match ns-3 TcpCubic defaults)
    bool m_fastConvergence{true};
    bool m_tcpFriendliness{false};
    double m_beta{0.7};
    double m_c{0.4};
    uint8_t m_cntClamp{20};
    Time m_cubicDelta{MilliSeconds(10)};

    // Core window state (bytes unless noted)
    uint32_t m_cwndBytes{0};
    uint32_t m_ssThreshBytes{0};
    uint32_t m_initialCwndBytes{0};

    // CUBIC internal state (mostly in segments, mirroring TcpCubic)
    uint32_t m_cWndCnt{0};
    uint32_t m_lastMaxCwnd{0};    // segments
    uint32_t m_bicOriginPoint{0}; // segments
    double m_bicK{0.0};
    Time m_delayMin{Time::Min()};
    Time m_epochStart{Time::Min()};
    uint32_t m_ackCnt{0};
    uint32_t m_tcpCwnd{0}; // segments (reno-friendly estimate)

    // Simple guard to avoid repeated reductions on duplicate OmniNACKs
    uint64_t m_lastReductionAck{0};
};

} // namespace ns3

#endif // OMNI_RDMA_CUBIC_H
