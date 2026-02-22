#ifndef SWITCH_PACKET_DROPPER_H
#define SWITCH_PACKET_DROPPER_H

#include "ns3/object.h"
#include "ns3/type-id.h"
#include <random>
#include <iostream>
#include <vector>
#include <array>
#include <string>  // 
#include <algorithm>

namespace ns3 {

class PacketDropper : public Object
{
public:
  static TypeId GetTypeId (void);

  // 默认构造函数，提供默认参数
  PacketDropper ();

  // 带参数构造函数
  PacketDropper(double overall_drop_rate,
                int burst_n, int burst_offset,
                int high_freq_m, int high_freq_min, int high_freq_max,
                const std::string &distribution);

  void setParam(double overall_drop_rate,
                    int burst_n, int burst_offset,
                    int high_freq_m, int high_freq_min, int high_freq_max,
                    unsigned int seed = 12345);
  /**
   * @brief 每收到一个数据包调用
   * @return 0：不丢包，1：随机丢包，2：高频丢包，3：片段丢包
   */
  int assertDrop ();
  /**
   * @brief 用于打印当前变量状态
   */
  void PrintStatus () const;
  /**
   * @brief 设置丢包分布策略
   * @param distribution 分布策略字符串，例如 "amazon", "google", "microsoft"
   */
  void setDropDistribution (const std::string &distribution);
  double GetDropRate () const;
  void SetDropRate (double rate);

  // 新增的字符串成员，存储丢包分布类型
  std::string dropDistribution;

private:
  // 内部工具函数
  bool triggerEvent (double prob);
  int uniformInt (int low, int high);
  double uniformReal (double low, double high);

  // 随机数生成器
  std::mt19937 rng;

  // 状态变量
  int burst_remaining;                  // 片段丢包事件剩余待丢弃的包数
  std::vector<bool> high_freq_schedule; // 高频丢包事件的调度表
  int high_freq_index;                  // 当前调度表的下标

  // 配置参数
  double overall_drop_rate;   // 整体丢包率（每个包触发丢包事件的概率按事件内平均丢包数折算）
  double event_ratio_burst;   // 片段丢包事件比例
  double event_ratio_highfreq;// 高频丢包事件比例
  double event_ratio_random;  // 随机丢包事件比例
  int burst_n;                // 片段丢包的基准丢包数（平均值）
  int burst_offset;           // 片段丢包事件丢包数的偏移范围（实际丢包数在 [burst_n - burst_offset, burst_n + burst_offset]）
  int high_freq_m;            // 高频丢包事件中参与判断的包数
  int high_freq_min;          // 高频丢包事件中最小丢包数
  int high_freq_max;          // 高频丢包事件中最大丢包数

  double average_drop_per_event; // 每个事件的平均丢包数
  double event_probability;      // 每个包触发丢包事件的概率

  // 各种丢包类型的占比：random, multi, outage
  // std::array<double, 3> event_percentage;

    /**
   * @brief 根据 dropDistribution 的值初始化相关设置
   *
   * 为了使用 switch-case，本函数先将字符串转换为整型标识：
   *  0：amazon
   *  1：google
   *  2：microsoft
   *  -1：未知
   */
  void InitDistribution ();
};




} // namespace ns3

#endif // SWITCH_PACKET_DROPPER_H
