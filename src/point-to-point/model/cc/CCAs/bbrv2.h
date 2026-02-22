#ifndef BBR2_H
#define BBR2_H

#include "ns3/object.h"
#include "ns3/nstime.h"

namespace ns3 {

class Bbr2 : public Object
{
public:
  static TypeId GetTypeId (void);
  Bbr2 ();
  virtual ~Bbr2 ();

  // 返回对象占用内存大小
  static size_t bbr2_size ();

  // 初始化BBR2算法状态
  void bbr2_init ();

  // 接收到ACK时更新状态，参数：确认字节数和往返时延
  void bbr2_on_ack (uint32_t ackedBytes, Time rtt);

  // 获取当前拥塞窗口（单位：字节）
  uint32_t bbr2_get_cwnd () const;

  // 获取当前发送速率（单位：字节/秒）
  double bbr2_get_pacing_rate () const;

  // 获取当前带宽估计（单位：字节/秒）
  double bbr2_get_bandwidth () const;

  // 从空闲状态重启算法（例如重新进入慢启动阶段）
  void bbr2_restart_from_idle ();

  // 处理丢包事件，参数：丢失的字节数
  void bbr2_on_lost (uint32_t lostBytes);

  // 重置拥塞窗口到初始值
  void bbr2_reset_cwnd ();

  // 输出或回调BBR2当前的状态信息
  void bbr2_info_cb () const;

  // 判断当前是否处于恢复状态
  bool bbr2_in_recovery () const;

  // 判断当前是否处于慢启动阶段
  bool bbr2_in_slow_start () const;

private:
  uint32_t m_cwnd;         // 拥塞窗口，单位：字节
  double m_pacingRate;     // 发送速率，单位：字节/秒
  double m_bandwidth;      // 带宽估计，单位：字节/秒
  bool m_inRecovery;       // 是否处于恢复状态
  bool m_inSlowStart;      // 是否处于慢启动阶段
};

} // namespace ns3

#endif // BBR2_H
