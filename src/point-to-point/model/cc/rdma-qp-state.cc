/*
 * Copyright (c) 2018 Natale Patriciello <natale.patriciello@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "rdma-qp-state.h"

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(RdmaSocketState);

TypeId
RdmaSocketState::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::RdmaSocketState")
            .SetParent<Object>()
            .SetGroupName("PointToPoint")
            .AddConstructor<RdmaSocketState>()
            .AddAttribute("EnablePacing",
                          "Enable Pacing",
                          BooleanValue(false),
                          MakeBooleanAccessor(&RdmaSocketState::m_pacing),
                          MakeBooleanChecker())
            .AddAttribute("MaxPacingRate",
                          "Set Max Pacing Rate",
                          DataRateValue(DataRate("4Gb/s")),
                          MakeDataRateAccessor(&RdmaSocketState::m_maxPacingRate),
                          MakeDataRateChecker())
            .AddAttribute("PacingSsRatio",
                          "Percent pacing rate increase for slow start conditions",
                          UintegerValue(200),
                          MakeUintegerAccessor(&RdmaSocketState::m_pacingSsRatio),
                          MakeUintegerChecker<uint16_t>())
            .AddAttribute("PacingCaRatio",
                          "Percent pacing rate increase for congestion avoidance conditions",
                          UintegerValue(120),
                          MakeUintegerAccessor(&RdmaSocketState::m_pacingCaRatio),
                          MakeUintegerChecker<uint16_t>())
            .AddAttribute("PaceInitialWindow",
                          "Perform pacing for initial window of data",
                          BooleanValue(false),
                          MakeBooleanAccessor(&RdmaSocketState::m_paceInitialWindow),
                          MakeBooleanChecker())
            .AddTraceSource("PacingRate",
                            "The current Rdma pacing rate",
                            MakeTraceSourceAccessor(&RdmaSocketState::m_pacingRate),
                            "ns3::TracedValueCallback::DataRate")
            .AddTraceSource("CongestionWindow",
                            "The Rdma connection's congestion window",
                            MakeTraceSourceAccessor(&RdmaSocketState::m_cWnd),
                            "ns3::TracedValueCallback::Uint32")
            .AddTraceSource("CongestionWindowInflated",
                            "The Rdma connection's inflated congestion window",
                            MakeTraceSourceAccessor(&RdmaSocketState::m_cWndInfl),
                            "ns3::TracedValueCallback::Uint32")
            .AddTraceSource("SlowStartThreshold",
                            "Rdma slow start threshold (bytes)",
                            MakeTraceSourceAccessor(&RdmaSocketState::m_ssThresh),
                            "ns3::TracedValueCallback::Uint32")
            .AddTraceSource("CongState",
                            "Rdma Congestion machine state",
                            MakeTraceSourceAccessor(&RdmaSocketState::m_congState),
                            "ns3::TracedValueCallback::RdmaCongState")
            .AddTraceSource("EcnState",
                            "Trace ECN state change of socket",
                            MakeTraceSourceAccessor(&RdmaSocketState::m_ecnState),
                            "ns3::TracedValueCallback::EcnState")
            .AddTraceSource("HighestSequence",
                            "Highest sequence number received from peer",
                            MakeTraceSourceAccessor(&RdmaSocketState::m_highTxMark),
                            "ns3::TracedValueCallback::SequenceNumber32")
            .AddTraceSource("NextTxSequence",
                            "Next sequence number to send (SND.NXT)",
                            MakeTraceSourceAccessor(&RdmaSocketState::m_nextTxSequence),
                            "ns3::TracedValueCallback::SequenceNumber32")
            .AddTraceSource("BytesInFlight",
                            "The Rdma connection's congestion window",
                            MakeTraceSourceAccessor(&RdmaSocketState::m_bytesInFlight),
                            "ns3::TracedValueCallback::Uint32")
            .AddTraceSource("RTT",
                            "Smoothed RTT",
                            MakeTraceSourceAccessor(&RdmaSocketState::m_srtt),
                            "ns3::TracedValueCallback::Time")
            .AddTraceSource("LastRTT",
                            "RTT of the last (S)ACKed packet",
                            MakeTraceSourceAccessor(&RdmaSocketState::m_lastRtt),
                            "ns3::TracedValueCallback::Time");
    return tid;
}

RdmaSocketState::RdmaSocketState(const RdmaSocketState& other)
    : Object(other),
      m_cWnd(other.m_cWnd),
      m_ssThresh(other.m_ssThresh),
      m_initialCWnd(other.m_initialCWnd),
      m_initialSsThresh(other.m_initialSsThresh),
      m_segmentSize(other.m_segmentSize),
      m_lastAckedSeq(other.m_lastAckedSeq),
      m_congState(other.m_congState),
      m_ecnState(other.m_ecnState),
      m_highTxMark(other.m_highTxMark),
      m_nextTxSequence(other.m_nextTxSequence),
      m_rcvTimestampValue(other.m_rcvTimestampValue),
      m_rcvTimestampEchoReply(other.m_rcvTimestampEchoReply),
      m_pacing(other.m_pacing),
      m_maxPacingRate(other.m_maxPacingRate),
      m_pacingRate(other.m_pacingRate),
      m_pacingSsRatio(other.m_pacingSsRatio),
      m_pacingCaRatio(other.m_pacingCaRatio),
      m_paceInitialWindow(other.m_paceInitialWindow),
      m_minRtt(other.m_minRtt),
      m_bytesInFlight(other.m_bytesInFlight),
      m_isCwndLimited(other.m_isCwndLimited),
      m_srtt(other.m_srtt),
      m_lastRtt(other.m_lastRtt),
      m_ecnMode(other.m_ecnMode),
      m_useEcn(other.m_useEcn),
      m_ectCodePoint(other.m_ectCodePoint),
      m_lastAckedSackedBytes(other.m_lastAckedSackedBytes)

{
}

const char* const RdmaSocketState::RdmaCongStateName[RdmaSocketState::CA_LAST_STATE] = {
    "CA_OPEN",
    "CA_DISORDER",
    "CA_CWR",
    "CA_RECOVERY",
    "CA_LOSS",
};

const char* const RdmaSocketState::EcnStateName[RdmaSocketState::ECN_CWR_SENT + 1] = {
    "ECN_DISABLED",
    "ECN_IDLE",
    "ECN_CE_RCVD",
    "ECN_SENDING_ECE",
    "ECN_ECE_RCVD",
    "ECN_CWR_SENT",
};

} // namespace ns3
