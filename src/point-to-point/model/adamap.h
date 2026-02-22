#ifndef ADAMAP_H
#define ADAMAP_H

#include <vector>
#include <stdint.h>
#include <ns3/simulator.h>

namespace ns3 {

// # define OMNI_DETAIL        // 打印详细信息

struct Adamap 
{
  uint32_t id;
  std::vector<bool> bitmap; ///< 保存bitmap内容（1表示收到）
  uint32_t startSeq;        ///< 该bitmap对应的起始序列号
  uint32_t reprLength;      ///< 当前bitmap表示的长度（可能大于bitmap长度，表示连续丢包）
};

struct Adamap_with_index {
  Adamap adamap;
  int32_t tableIndex;
  bool isFinish;
  Time lastCallTime;
  int max_retrans_omni_type;
  // Adamap_with_index () {}
  // Adamap_with_index(int32_t index) {tableIndex = index; isFinish = false;}
};

inline void PrintAdamap(const Adamap* adamap, const std::string& name, FILE* fout=NULL) {
  if (fout == NULL) {
    // 输出到控制台
    std::cout << "  --------  " << name << "  ----------:\n";
    std::cout << "  Adamap ID: " << adamap->id << std::endl;
    std::cout << "  Start Sequence: " << adamap->startSeq << std::endl;
    std::cout << "  Representation Length: " << adamap->reprLength << " (" << adamap->startSeq + 1 << " ~ " << adamap->startSeq + adamap->reprLength << ") ("
    << adamap->reprLength - (uint32_t)std::count((adamap->bitmap).begin(), adamap->bitmap.end(), true)
    << ")" << std::endl;
    std::cout << "  Bitmap: ";
    for (bool bit : adamap->bitmap) {
        std::cout << (bit ? "1" : "0");
    }
    std::cout << "(size()=" << adamap->bitmap.size() << ")" << std::endl;
    std::cout << "  --------------------------------------:\n";
  }
  else {
    // 输出到文件
    fprintf(fout, "  --------  %s  ----------:\n", name.c_str());
    fprintf(fout, "  Adamap ID: %d\n", adamap->id);
    fprintf(fout, "  Start Sequence: %d\n", adamap->startSeq);
    fprintf(fout, "  Representation Length: %u (%u ~ %u) (%u)\n", adamap->reprLength, adamap->startSeq + 1, adamap->startSeq + adamap->reprLength,
      uint32_t(adamap->reprLength - std::count((adamap->bitmap).begin(), adamap->bitmap.end(), true)));
    fprintf(fout, "  Bitmap: ");
    for (bool bit : adamap->bitmap) {
        fprintf(fout, "%c", bit ? '1' : '0');
    }
    fprintf(fout, "(size()=%lu)\n", adamap->bitmap.size());
    fprintf(fout, "  ---------------------------------------:\n");
  }
}

/**
 * @brief 将 std::vector<bool> 转换为 uint64_t
 * @param bitmap 需要转换的 bitmap
 * @return 转换后的 uint64_t 结果
 */
inline uint64_t BitmapToUint64(const std::vector<bool>& bitmap) {
  uint64_t result = 0;
  size_t len = bitmap.size() > 64 ? 64 : bitmap.size(); // 限制长度不超过 64 位

  for (size_t i = 0; i < len; ++i) {
      if (bitmap[i]) {
          result |= (1ULL << i); // 设置第 i 位为 1
      }
  }
  return result;
}

/**
* @brief 将 uint64_t 转换为 std::vector<bool>
* @param value 需要转换的 uint64_t 数字
* @param size  目标 bitmap 的大小（必须 ≤ 64）
* @return 转换后的 bitmap
*/
inline std::vector<bool> Uint64ToBitmap(uint64_t value, size_t size=16) {
  std::vector<bool> bitmap(size, false); // 先初始化为全 0
  if (size > 64) size = 64; // 限制最大 64 位

  for (size_t i = 0; i < size; ++i) {
      bitmap[i] = (value & (1ULL << i)) != 0; // 取第 i 位
  }
  return bitmap;
}

} // namespace ns3

#endif // ADAMAP_H
