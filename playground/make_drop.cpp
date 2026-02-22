#include <iostream>
#include <random>
#include <vector>
#include <algorithm>

class PacketDropper {
public:
    // 构造函数参数说明：
    // overall_drop_rate：整体丢包率（以包为单位），例如 0.001 表示平均每 1000 个包丢 1 个
    // event_ratio_burst, event_ratio_highfreq, event_ratio_random：
    //     各类丢包事件在所有事件中所占比例（总和应为 1）
    // burst_n: 片段丢包事件的基准丢包数（平均值）
    // burst_offset: 片段丢包事件中丢包数的偏移范围（实际丢包数在 [burst_n - burst_offset, burst_n + burst_offset]）
    // high_freq_m: 高频丢包事件中参与判断的包数
    // high_freq_min, high_freq_max: 高频丢包事件中在 high_freq_m 个包内随机丢弃的包数范围
    // seed: 随机种子，保证可复现性
    PacketDropper(double overall_drop_rate,
                  double event_ratio_random, double event_ratio_highfreq, double event_ratio_burst,
                  int burst_n, int burst_offset,
                  int high_freq_m, int high_freq_min, int high_freq_max,
                  unsigned int seed = 12345)
        : overall_drop_rate(overall_drop_rate),
          event_ratio_burst(event_ratio_burst),
          event_ratio_highfreq(event_ratio_highfreq),
          event_ratio_random(event_ratio_random),
          burst_n(burst_n),
          burst_offset(burst_offset),
          high_freq_m(high_freq_m),
          high_freq_min(high_freq_min),
          high_freq_max(high_freq_max),
          burst_remaining(0),
          high_freq_index(0)
    {
        rng.seed(seed);
        // 计算各事件的平均丢包数：
        // 片段丢包事件的平均丢包数为 burst_n（假设偏移量为对称分布，平均为 0）
        double avg_burst = static_cast<double>(burst_n);
        // 高频丢包事件的平均丢包数
        double avg_highfreq = (high_freq_min + high_freq_max) / 2.0;
        // 随机丢包事件仅丢弃当前包，平均丢包数为 1
        double avg_random = 1.0;
        average_drop_per_event = event_ratio_burst * avg_burst +
                                 event_ratio_highfreq * avg_highfreq +
                                 event_ratio_random * avg_random;
        // 每个包触发一个丢包事件的概率
        event_probability = overall_drop_rate / average_drop_per_event;
    }

    // 修改后的 receive() 函数：
    // 返回值含义：
    // 0：不丢包
    // 1：随机丢包
    // 2：高频丢包
    // 3：片段丢包
    int receive() {
        // 1. 如果处于片段丢包事件状态，则直接丢包（返回 3）
        if (burst_remaining > 0) {
            burst_remaining--;
            return 3;
        }

        // 2. 如果处于高频丢包事件状态，则根据调度表判断
        if (!high_freq_schedule.empty()) {
            bool drop = high_freq_schedule[high_freq_index];
            high_freq_index++;
            if (high_freq_index >= high_freq_schedule.size()) {
                high_freq_schedule.clear();
                high_freq_index = 0;
            }
            return drop ? 2 : 0;
        }

        // 3. 当前不处于任何事件状态，则以 event_probability 触发一个丢包事件
        if (triggerEvent(event_probability)) {
            // 选择具体事件类型
            double r = uniformReal(0.0, 1.0);
            if (r < event_ratio_burst) {
                // 片段丢包事件：计算实际丢包数 = burst_n + offset（至少丢 1 个包）
                int offset = uniformInt(-burst_offset, burst_offset);
                int dropCount = burst_n + offset;
                if (dropCount < 1)
                    dropCount = 1;
                // 当前包丢弃，后续 burst_remaining 个包丢弃
                burst_remaining = dropCount - 1;
                return 3;
            } else if (r < event_ratio_burst + event_ratio_highfreq) {
                // 高频丢包事件：在 high_freq_m 个包中随机丢弃 dropCount 个
                int dropCount = uniformInt(high_freq_min, high_freq_max);
                if (dropCount > high_freq_m)
                    dropCount = high_freq_m;
                high_freq_schedule.assign(high_freq_m, false);
                for (int i = 0; i < dropCount; i++) {
                    high_freq_schedule[i] = true;
                }
                std::shuffle(high_freq_schedule.begin(), high_freq_schedule.end(), rng);
                high_freq_index = 0;
                // 根据调度表判断当前包是否丢弃
                bool drop = high_freq_schedule[high_freq_index];
                high_freq_index++;
                return drop ? 2 : 0;
            } else {
                // 随机丢包事件：直接丢弃当前包
                return 1;
            }
        }

        // 4. 没有触发任何丢包事件，正常接收包
        return 0;
    }

private:
    // 随机数生成器及内部状态
    std::mt19937 rng;
    int burst_remaining;                  // 片段丢包事件中剩余待丢弃的包数
    std::vector<bool> high_freq_schedule; // 高频丢包事件的调度表
    int high_freq_index;                  // 当前调度表的下标

    // 配置参数
    double overall_drop_rate;
    double event_ratio_burst;
    double event_ratio_highfreq;
    double event_ratio_random;
    int burst_n;
    int burst_offset;
    int high_freq_m;
    int high_freq_min;
    int high_freq_max;
    double average_drop_per_event;  // 每个事件的平均丢包数
    double event_probability;       // 每个包触发事件的概率

    // 以 prob 的概率返回 true
    bool triggerEvent(double prob) {
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        return dist(rng) < prob;
    }

    // 生成 [low, high] 范围内的随机整数
    int uniformInt(int low, int high) {
        std::uniform_int_distribution<int> dist(low, high);
        return dist(rng);
    }
    
    // 生成 [low, high) 范围内的随机实数
    double uniformReal(double low, double high) {
        std::uniform_real_distribution<double> dist(low, high);
        return dist(rng);
    }
};

int main() {
    // 示例配置：
    // 整体丢包率为 0.1%（即 0.001）
    // 各种丢包类型的占比：random, multi, outage
    double amazon[3] = {0.53, 0.12, 0.35};
    double google[3] = {0.37, 0.51, 0.12};
    double microsoft[3] = {0.68, 0.19, 0.13};
    // 片段丢包参数：burst_n = 10, burst_offset = 3 （实际丢包数在 7 ~ 13 之间）
    // 高频丢包参数：high_freq_m = 20, 丢包数在 [5, 10] 之间
    // 固定随机种子 12345 保证可复现性
    PacketDropper dropper(0.001, 0.4, 0.3, 0.3,
                            15, 3,
                            15, 2, 14,
                            43);

    // 模拟接收 10000 个包，并输出各类型丢包事件统计
    int totalPackets = 100000000;
    int countReceived = 0, countRandom = 0, countHighFreq = 0, countBurst = 0;
    for (int i = 0; i < totalPackets; i++) {
        int res = dropper.receive();
        if (res == 0) {
            // std::cout << "Packet " << i << " received.\n";
            countReceived++;
        } else if (res == 1) {
            std::cout << "Packet " << i << " random drop.\n";
            countRandom++;
        } else if (res == 2) {
            std::cout << "Packet " << i << " high frequency drop.\n";
            countHighFreq++;
        } else if (res == 3) {
            std::cout << "Packet " << i << " burst drop.\n";
            countBurst++;
        }
    }

    std::cout << "Total received: " << countReceived
              << ", random drop: " << countRandom
              << ", high frequency drop: " << countHighFreq
              << ", burst drop: " << countBurst << "\n";
    return 0;
}
