#include "ns3/bbrv2.h"
#include "ns3/log.h"
#include "ns3/simulator.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("Bbr2");
NS_OBJECT_ENSURE_REGISTERED (Bbr2);

TypeId
Bbr2::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::Bbr2")
    .SetParent<Object> ()
    .SetGroupName ("Internet")
    .AddConstructor<Bbr2> ()
    ;
  return tid;
}

Bbr2::Bbr2 ()
  : m_cwnd (10000),        // 初始拥塞窗口设定（单位字节）
    m_pacingRate (100000.0), // 初始发送速率（字节/秒）
    m_bandwidth (100000.0),  // 初始带宽估计（字节/秒）
    m_inRecovery (false),
    m_inSlowStart (true)
{
  NS_LOG_FUNCTION (this);
  bbr2_init ();
}

Bbr2::~Bbr2 ()
{
  NS_LOG_FUNCTION (this);
}

size_t
Bbr2::bbr2_size ()
{
  return sizeof (Bbr2);
}

void
Bbr2::bbr2_init ()
{
  NS_LOG_FUNCTION (this);
  // 初始化内部状态，实际实现时可参考xqc_bbr2.c中的逻辑
  m_cwnd = 10000;
  m_pacingRate = 100000.0;
  m_bandwidth = 100000.0;
  m_inRecovery = false;
  m_inSlowStart = true;
}

void
Bbr2::bbr2_on_ack (uint32_t ackedBytes, Time rtt)
{
  NS_LOG_FUNCTION (this << ackedBytes << rtt.GetSeconds ());
  // 简化逻辑：在慢启动阶段，直接增加拥塞窗口
  if (m_inSlowStart)
    {
      m_cwnd += ackedBytes;
      // 当拥塞窗口超过一定阈值后退出慢启动（阈值为示例值）
      if (m_cwnd > 50000)
        {
          m_inSlowStart = false;
        }
    }
  else
    {
      // 拥塞避免阶段的增长算法（此处为简化示例）
      m_cwnd += (ackedBytes * ackedBytes) / m_cwnd;
    }

  // 更新带宽估计：以本次ACK确认字节和RTT简单计算
  double currentBw = static_cast<double> (ackedBytes) / rtt.GetSeconds ();
  if (currentBw > m_bandwidth)
    {
      m_bandwidth = currentBw;
    }
  // 将发送速率与带宽估计关联
  m_pacingRate = m_bandwidth;
}

uint32_t
Bbr2::bbr2_get_cwnd () const
{
  NS_LOG_FUNCTION (this);
  return m_cwnd;
}

double
Bbr2::bbr2_get_pacing_rate () const
{
  NS_LOG_FUNCTION (this);
  return m_pacingRate;
}

double
Bbr2::bbr2_get_bandwidth () const
{
  NS_LOG_FUNCTION (this);
  return m_bandwidth;
}

void
Bbr2::bbr2_restart_from_idle ()
{
  NS_LOG_FUNCTION (this);
  // 从空闲状态恢复时重置慢启动及拥塞窗口
  m_inSlowStart = true;
  m_cwnd = 10000;
}

void
Bbr2::bbr2_on_lost (uint32_t lostBytes)
{
  NS_LOG_FUNCTION (this << lostBytes);
  // 遇到丢包事件时进入恢复状态，并适当降低拥塞窗口
  m_inRecovery = true;
  if (m_cwnd > lostBytes)
    {
      m_cwnd -= lostBytes;
    }
  else
    {
      m_cwnd = 1000; // 设置一个最小值
    }
}

void
Bbr2::bbr2_reset_cwnd ()
{
  NS_LOG_FUNCTION (this);
  // 重置拥塞窗口到初始默认值
  m_cwnd = 10000;
}

void
Bbr2::bbr2_info_cb () const
{
  NS_LOG_FUNCTION (this);
  NS_LOG_INFO ("BBR Info: cwnd = " << m_cwnd <<
               ", pacingRate = " << m_pacingRate <<
               ", bandwidth = " << m_bandwidth);
}

bool
Bbr2::bbr2_in_recovery () const
{
  NS_LOG_FUNCTION (this);
  return m_inRecovery;
}

bool
Bbr2::bbr2_in_slow_start () const
{
  NS_LOG_FUNCTION (this);
  return m_inSlowStart;
}

} // namespace ns3
