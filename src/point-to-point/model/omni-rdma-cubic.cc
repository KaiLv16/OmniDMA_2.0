#include "omni-rdma-cubic.h"

#include "rdma-queue-pair.h"

#include "ns3/boolean.h"
#include "ns3/double.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("OmniRdmaCubic");
NS_OBJECT_ENSURE_REGISTERED(OmniRdmaCubic);

TypeId
OmniRdmaCubic::GetTypeId(void)
{
    static TypeId tid =
        TypeId("ns3::OmniRdmaCubic")
            .SetParent<Object>()
            .AddConstructor<OmniRdmaCubic>()
            .SetGroupName("PointToPoint")
            .AddAttribute("FastConvergence",
                          "Enable (true) or disable (false) fast convergence",
                          BooleanValue(true),
                          MakeBooleanAccessor(&OmniRdmaCubic::m_fastConvergence),
                          MakeBooleanChecker())
            .AddAttribute("TcpFriendliness",
                          "Enable (true) or disable (false) TCP friendliness",
                          BooleanValue(false),
                          MakeBooleanAccessor(&OmniRdmaCubic::m_tcpFriendliness),
                          MakeBooleanChecker())
            .AddAttribute("Beta",
                          "Beta for multiplicative decrease and fast-convergence behavior",
                          DoubleValue(0.7),
                          MakeDoubleAccessor(&OmniRdmaCubic::m_beta),
                          MakeDoubleChecker<double>(0.0))
            .AddAttribute("C",
                          "CUBIC scaling factor used in W_cubic(t), 0.4 by default, 2500 to observe ~1s K value with 1ms RTT",
                          DoubleValue(25000),
                          MakeDoubleAccessor(&OmniRdmaCubic::m_c),
                          MakeDoubleChecker<double>(0.0))
            .AddAttribute("CntClamp",
                          "Counter clamp used before first loss to bound cwnd growth pace",
                          UintegerValue(20),
                          MakeUintegerAccessor(&OmniRdmaCubic::m_cntClamp),
                          MakeUintegerChecker<uint8_t>())
            .AddAttribute("CubicDelta",
                          "Time to wait after recovery before refreshing delayMin",
                          TimeValue(MilliSeconds(10)),
                          MakeTimeAccessor(&OmniRdmaCubic::m_cubicDelta),
                          MakeTimeChecker());
    return tid;
}

void
OmniRdmaCubic::Initialize(Ptr<RdmaQueuePair> qp, uint32_t segmentSize, uint32_t initialWindowBytes)
{
    m_segmentSize = std::max(1u, segmentSize);
    m_initialCwndBytes = initialWindowBytes > 0 ? initialWindowBytes : 10 * m_segmentSize;
    m_cwndBytes = std::max(2u * m_segmentSize, m_initialCwndBytes);
    m_ssThreshBytes = std::numeric_limits<uint32_t>::max() / 2;

    m_cWndCnt = 0;
    m_lastMaxCwnd = 0;
    m_bicOriginPoint = 0;
    m_bicK = 0.0;
    m_delayMin = Time::Min();
    m_epochStart = Time::Min();
    m_ackCnt = 0;
    m_tcpCwnd = 0;
    m_lastReductionAck = 0;
    m_initialized = true;

    ApplyWindow(qp);
}

void
OmniRdmaCubic::ApplyWindow(Ptr<RdmaQueuePair> qp) const
{
    if (qp == nullptr)
    {
        return;
    }
    qp->m_win = std::max(m_cwndBytes, 2u * m_segmentSize);
}

void
OmniRdmaCubic::PktsAcked(const Time& rtt)
{
    if (!rtt.IsPositive())
    {
        return;
    }

    if (m_epochStart != Time::Min() && (Simulator::Now() - m_epochStart) < m_cubicDelta)
    {
        return;
    }

    if (m_delayMin == Time::Min() || m_delayMin > rtt)
    {
        m_delayMin = rtt;
    }
}

uint32_t
OmniRdmaCubic::UpdateCnt(uint32_t segmentsAcked)
{
    uint32_t segCwnd = std::max(1u, m_cwndBytes / m_segmentSize);
    uint32_t delta;
    uint32_t bicTarget;
    uint32_t cnt;
    uint32_t maxCnt;
    double offs;

    m_ackCnt += segmentsAcked;

    if (m_epochStart == Time::Min())
    {
        m_epochStart = Simulator::Now();
        m_ackCnt = segmentsAcked;
        m_tcpCwnd = segCwnd;

        if (m_lastMaxCwnd <= segCwnd)
        {
            m_bicK = 0.0;
            m_bicOriginPoint = segCwnd;
        }
        else
        {
            m_bicK = std::pow((m_lastMaxCwnd - segCwnd) / m_c, 1.0 / 3.0);
            m_bicOriginPoint = m_lastMaxCwnd;
        }
    }

    Time delay = (m_delayMin == Time::Min()) ? Seconds(0) : m_delayMin;
    Time t = Simulator::Now() + delay - m_epochStart;
    if (t.GetSeconds() < m_bicK)
    {
        offs = m_bicK - t.GetSeconds();
    }
    else
    {
        offs = t.GetSeconds() - m_bicK;
    }

    delta = static_cast<uint32_t>(m_c * std::pow(offs, 3));
    bicTarget = (t.GetSeconds() < m_bicK) ? (m_bicOriginPoint - delta) : (m_bicOriginPoint + delta);

    if (bicTarget > segCwnd)
    {
        cnt = segCwnd / (bicTarget - segCwnd);
    }
    else
    {
        cnt = 100 * segCwnd;
    }

    if (m_lastMaxCwnd == 0 && cnt > m_cntClamp)
    {
        cnt = m_cntClamp;
    }

    if (m_tcpFriendliness)
    {
        auto scale = static_cast<uint32_t>(8 * (1024 + m_beta * 1024) / 3 / (1024 - m_beta * 1024));
        delta = (segCwnd * scale) >> 3;
        while (delta > 0 && m_ackCnt > delta)
        {
            m_ackCnt -= delta;
            m_tcpCwnd++;
        }
        if (m_tcpCwnd > segCwnd)
        {
            delta = m_tcpCwnd - segCwnd;
            maxCnt = segCwnd / delta;
            if (cnt > maxCnt)
            {
                cnt = maxCnt;
            }
        }
    }

    return std::max(cnt, 2u);
}

void
OmniRdmaCubic::EnterLoss(uint64_t ackSeq, uint32_t bytesInFlight)
{
    uint32_t segCwnd = std::max(1u, m_cwndBytes / m_segmentSize);

    if (ackSeq <= m_lastReductionAck)
    {
        return;
    }
    m_lastReductionAck = ackSeq;

    if (segCwnd < m_lastMaxCwnd && m_fastConvergence)
    {
        m_lastMaxCwnd = static_cast<uint32_t>((segCwnd * (1 + m_beta)) / 2);
    }
    else
    {
        m_lastMaxCwnd = segCwnd;
    }

    m_epochStart = Time::Min();
    m_bicOriginPoint = 0;
    m_bicK = 0.0;
    m_ackCnt = 0;
    m_tcpCwnd = 0;
    m_delayMin = Time::Min();
    m_cWndCnt = 0;

    uint32_t ssThresh =
        std::max(static_cast<uint32_t>(segCwnd * m_beta), 2u) * m_segmentSize;
    m_ssThreshBytes = std::max(ssThresh, 2u * m_segmentSize);

    if (bytesInFlight > 0)
    {
        m_cwndBytes = std::min(std::max(bytesInFlight, 2u * m_segmentSize), m_ssThreshBytes);
    }
    else
    {
        m_cwndBytes = m_ssThreshBytes;
    }
}

void
OmniRdmaCubic::OnAck(Ptr<RdmaQueuePair> qp,
                     uint32_t ackedBytes,
                     uint64_t ackSeq,
                     uint32_t priorInFlight,
                     const Time& rtt,
                     bool lossSignal)
{
    if (!m_initialized)
    {
        Initialize(qp, m_segmentSize, 0);
    }
    m_segmentSize = std::max(m_segmentSize, 1u);

    PktsAcked(rtt);

    if (lossSignal)
    {
        EnterLoss(ackSeq, priorInFlight);
    }

    if (ackedBytes == 0)
    {
        ApplyWindow(qp);
        return;
    }

    bool inSlowStart = m_cwndBytes < m_ssThreshBytes;
    bool isCwndLimited = priorInFlight + m_segmentSize >= m_cwndBytes;
    if (!inSlowStart && !isCwndLimited)
    {
        ApplyWindow(qp);
        return;
    }

    if (inSlowStart)
    {
        m_cwndBytes += ackedBytes;
    }
    else
    {
        uint32_t segmentsAcked = std::max(1u, ackedBytes / m_segmentSize);
        m_cWndCnt += segmentsAcked;
        uint32_t cnt = UpdateCnt(segmentsAcked);
        if (m_cWndCnt >= cnt)
        {
            m_cwndBytes += m_segmentSize;
            m_cWndCnt -= cnt;
        }
    }

    ApplyWindow(qp);
}

void
OmniRdmaCubic::OnTimeout(Ptr<RdmaQueuePair> qp, uint64_t ackSeq, uint32_t bytesInFlight)
{
    if (!m_initialized)
    {
        return;
    }
    EnterLoss(ackSeq + 1, bytesInFlight);
    ApplyWindow(qp);
}

bool
OmniRdmaCubic::IsInitialized() const
{
    return m_initialized;
}

uint32_t
OmniRdmaCubic::GetCwndBytes() const
{
    return m_cwndBytes;
}

uint32_t
OmniRdmaCubic::GetSsThreshBytes() const
{
    return m_ssThreshBytes;
}

} // namespace ns3
