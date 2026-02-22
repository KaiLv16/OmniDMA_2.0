#include "switch-packet-dropper.h"
#include "ns3/log.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/boolean.h"
#include "ns3/pointer.h"
#include "ns3/integer.h"
#include "ns3/string.h"
#include <algorithm>
#include <iostream>
#include <random>
#include <vector>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("PacketDropper");

TypeId
PacketDropper::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::PacketDropper")
    .SetParent<Object> ()
    .SetGroupName ("YourGroupName")
    .AddConstructor<PacketDropper> ()
    .AddAttribute("DropRate", "Drop Rate of this port (total)",
              DoubleValue(0.0),  // 默认值为 671.0
              MakeDoubleAccessor(&PacketDropper::GetDropRate, &PacketDropper::SetDropRate),
              MakeDoubleChecker<double>())
    ;
  return tid;
}
double PacketDropper::GetDropRate () const {
  return overall_drop_rate;
}
void PacketDropper::SetDropRate (double rate)
{
  overall_drop_rate = rate;
  // 根据新的 m_dropRate 计算 m_dropInterval 或其他相关变量
  event_probability = overall_drop_rate / average_drop_per_event;
}

// 默认构造函数，提供一些默认参数
PacketDropper::PacketDropper ()
  : overall_drop_rate (0.001),
    event_ratio_burst (0.4),
    event_ratio_highfreq (0.3),
    event_ratio_random (0.3),
    burst_n (10),
    burst_offset (3),
    high_freq_m (20),
    high_freq_min (5),
    high_freq_max (10),
    burst_remaining (0),
    high_freq_index (0)
{
  rng.seed (12345);
  dropDistribution = "amazon"; // 默认赋值，可在外部修改
  double avg_burst = static_cast<double> (burst_n);
  double avg_highfreq = (high_freq_min + high_freq_max) / 2.0;
  double avg_random = 1.0;
  average_drop_per_event = event_ratio_burst * avg_burst +
                           event_ratio_highfreq * avg_highfreq +
                           event_ratio_random * avg_random;
  event_probability = overall_drop_rate / average_drop_per_event;

  // 根据 dropDistribution 的值进行初始化
  InitDistribution();
}

// 带参数构造函数
PacketDropper::PacketDropper(double overall_drop_rate,
                int burst_n, int burst_offset,
                int high_freq_m, int high_freq_min, int high_freq_max,
                const std::string &distribution)
  : overall_drop_rate (overall_drop_rate),
    burst_n (burst_n),
    burst_offset (burst_offset),
    high_freq_m (high_freq_m),
    high_freq_min (high_freq_min),
    high_freq_max (high_freq_max),
    burst_remaining (0),
    high_freq_index (0)
{
  rng.seed (44);
  dropDistribution = distribution;
  InitDistribution();
  double avg_burst = static_cast<double> (burst_n);
  double avg_highfreq = (high_freq_min + high_freq_max) / 2.0;
  double avg_random = 1.0;
  average_drop_per_event = event_ratio_burst * avg_burst +
                           event_ratio_highfreq * avg_highfreq +
                           event_ratio_random * avg_random;
  event_probability = overall_drop_rate / average_drop_per_event;
}


void PacketDropper::setParam(double overall_drop_rate,
    int burst_n, int burst_offset,
    int high_freq_m, int high_freq_min, int high_freq_max,
    unsigned int seed) 
{
    InitDistribution();
    overall_drop_rate = overall_drop_rate;
    burst_n = burst_n;
    burst_offset = burst_offset;
    high_freq_m = high_freq_m;
    high_freq_min = high_freq_min;
    high_freq_max = high_freq_max;
    burst_remaining = 0;
    high_freq_index = 0;

    rng.seed (seed);
    double avg_burst = static_cast<double> (burst_n);
    double avg_highfreq = (high_freq_min + high_freq_max) / 2.0;
    double avg_random = 1.0;
    average_drop_per_event = event_ratio_burst * avg_burst +
                            event_ratio_highfreq * avg_highfreq +
                            event_ratio_random * avg_random;
    event_probability = overall_drop_rate / average_drop_per_event;
}


// void PacketDropper::setDropDistribution (const std::string &distribution)
// {
//   dropDistribution = distribution;
//   // 设置新策略后调用初始化函数
//   InitDistribution();
// }


int PacketDropper::assertDrop() 
{
    // printf("call assertDrop() ");
  // 1. 如果处于片段丢包事件状态，直接丢包，返回 3
  if (burst_remaining > 0) {
    --burst_remaining;
    return 3;
  }

  // 2. 如果处于高频丢包事件状态，根据调度表判断
  if (!high_freq_schedule.empty()) {
    bool drop = high_freq_schedule[high_freq_index++];
    // 如果已经处理完所有调度项，则清空调度表
    if (high_freq_index >= high_freq_schedule.size()) {
      high_freq_schedule.clear();
      high_freq_index = 0;
    }
    return drop ? 2 : 0;
  }

  // 3. 当前不处于任何事件中，以 event_probability 触发一个丢包事件
  if (triggerEvent(event_probability)) {
    double r = uniformReal(0.0, 1.0);
    if (r < event_ratio_burst) {
      // 片段丢包事件：计算实际丢包数 = burst_n + offset（至少丢 1 个包）
      // return 0;
      // printf("branch 1\n");
      int offset = uniformInt(-burst_offset, burst_offset);
      int dropCount = burst_n + offset;
      if (dropCount < 1)
        dropCount = 1;
      burst_remaining = dropCount - 1;
      return 3;
    }
    else if (r < event_ratio_burst + event_ratio_highfreq) {
      // 高频丢包事件：在 high_freq_m 个包中随机丢弃 dropCount 个
      // printf("branch 2\n");
      int dropCount = uniformInt(high_freq_min, high_freq_max);
      
      if (dropCount > high_freq_m)
        dropCount = high_freq_m;
    
      // 如果 vector 大小不匹配则调整为固定大小 high_freq_m，
      // 否则直接使用已分配好的内存，减少重复分配操作
      if (static_cast<int>(high_freq_schedule.size()) != high_freq_m) {
        high_freq_schedule.resize(high_freq_m, false);
      }
      // 重置整个 vector，先全部置 false，然后前 dropCount 个置 true
      std::fill(high_freq_schedule.begin(), high_freq_schedule.end(), false);
      std::fill(high_freq_schedule.begin(), high_freq_schedule.begin() + dropCount, true);
      std::shuffle(high_freq_schedule.begin(), high_freq_schedule.end(), rng);
      
      high_freq_index = 0;
      bool drop = high_freq_schedule[high_freq_index++];
      return drop ? 2 : 0;
    }
    else {
      // 随机丢包事件：直接丢弃当前包，返回 1
      // printf("branch 3\n");
      return 1;
    }
  }

  // 4. 没有触发任何丢包事件，则正常接收包，返回 0
  // printf("branch 0\n");
  return 0;
}


/**
 * @brief 根据 dropDistribution 的值初始化相关设置
 *
 * 为了使用 switch-case，本函数先将字符串转换为整型标识：
 *  0：amazon
 *  1：google
 *  2：microsoft
 *  -1：未知
 */
void PacketDropper::InitDistribution ()
{
  if (dropDistribution == "amazon")
  {
    // event_percentage = {0.53, 0.12, 0.35};
    event_ratio_random = 0.53;
    event_ratio_highfreq = 0.12;
    event_ratio_burst = 0.35;
  }
  else if (dropDistribution == "google")
  {
    // event_percentage = {0.37, 0.51, 0.12};
    event_ratio_random = 0.37;
    event_ratio_highfreq = 0.51;
    event_ratio_burst = 0.12;
  }
  else if (dropDistribution == "microsoft")
  {
    // event_percentage = {0.68, 0.19, 0.13};
    event_ratio_random = 0.68;
    event_ratio_highfreq = 0.19;
    event_ratio_burst = 0.13;
  }
  else
  {
    NS_LOG_ERROR ("Unknown drop distribution: " << dropDistribution);
  }
}

bool PacketDropper::triggerEvent (double prob)
{
  std::uniform_real_distribution<double> dist (0.0, 1.0);
  return dist (rng) < prob;
}

// int PacketDropper::uniformInt (int low, int high)
// {
//   std::uniform_int_distribution<int> dist (low, high);
//   return dist (rng);
// }

int PacketDropper::uniformInt(int low, int high) {
    int range = high - low + 1;
    return low + (rand() % range);
}

double PacketDropper::uniformReal (double low, double high)
{
  std::uniform_real_distribution<double> dist (low, high);
  return dist (rng);
}

void PacketDropper::PrintStatus () const
{
  std::cout << "----- PacketDropper Status -----" << std::endl;
  std::cout << "overall_drop_rate: " << overall_drop_rate << std::endl;
  std::cout << "event_probability: " << event_probability << std::endl;
  std::cout << "dropDistribution: " << dropDistribution << std::endl;
  std::cout << "event_ratio_burst: " << event_ratio_burst << std::endl;
  std::cout << "event_ratio_highfreq: " << event_ratio_highfreq << std::endl;
  std::cout << "event_ratio_random: " << event_ratio_random << std::endl;
  std::cout << "burst_n: " << burst_n << std::endl;
  std::cout << "burst_offset: " << burst_offset << std::endl;
  std::cout << "high_freq_m: " << high_freq_m << std::endl;
  std::cout << "high_freq_min: " << high_freq_min << std::endl;
  std::cout << "high_freq_max: " << high_freq_max << std::endl;
  std::cout << "burst_remaining: " << burst_remaining << std::endl;
  std::cout << "high_freq_index: " << high_freq_index << std::endl;
  std::cout << "--------------------------------" << std::endl;
}

} // namespace ns3
