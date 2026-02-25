#include "switch-packet-dropper.h"
#include "ns3/log.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/boolean.h"
#include "ns3/pointer.h"
#include "ns3/integer.h"
#include "ns3/string.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <vector>

namespace ns3 {

namespace {

template <typename T>
bool
ParseScalar (const std::string &token, T *out)
{
  std::stringstream ss (token);
  ss >> *out;
  return !ss.fail ();
}

}  // namespace

NS_LOG_COMPONENT_DEFINE ("PacketDropper");

std::string PacketDropper::s_seqnumConfigLoadedPath = "";
std::string PacketDropper::s_timestepConfigLoadedPath = "";
bool PacketDropper::s_seqnumConfigLoaded = false;
bool PacketDropper::s_timestepConfigLoaded = false;
std::unordered_map<std::string, std::unordered_map<uint32_t, uint32_t> > PacketDropper::s_seqnumDropBudget;
std::unordered_map<std::string, PacketDropper::TimestepState> PacketDropper::s_timestepDropState;

TypeId
PacketDropper::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::PacketDropper")
    .SetParent<Object> ()
    .SetGroupName ("YourGroupName")
    .AddConstructor<PacketDropper> ()
    .AddAttribute ("DropRate", "Drop Rate of this port (total)",
                   DoubleValue (0.0),
                   MakeDoubleAccessor (&PacketDropper::GetDropRate, &PacketDropper::SetDropRate),
                   MakeDoubleChecker<double> ());
  return tid;
}

double
PacketDropper::GetDropRate () const
{
  return overall_drop_rate;
}

void
PacketDropper::SetDropRate (double rate)
{
  overall_drop_rate = rate;
  event_probability = (average_drop_per_event > 0.0) ? (overall_drop_rate / average_drop_per_event) : 0.0;
}

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
    high_freq_index (0),
    average_drop_per_event (0.0),
    event_probability (0.0),
    m_dropMode (DROP_MODE_LOSSRATE),
    m_dropModeName ("lossrate"),
    m_seqnumConfigFile ("config/config_drop_by_seqnum.txt"),
    m_timestepConfigFile ("config/config_drop_by_timestep.txt")
{
  rng.seed (12345);
  dropDistribution = "amazon";

  InitDistribution ();
  double avg_burst = static_cast<double> (burst_n);
  double avg_highfreq = (high_freq_min + high_freq_max) / 2.0;
  double avg_random = 1.0;
  average_drop_per_event = event_ratio_burst * avg_burst +
                           event_ratio_highfreq * avg_highfreq +
                           event_ratio_random * avg_random;
  event_probability = (average_drop_per_event > 0.0) ? (overall_drop_rate / average_drop_per_event) : 0.0;
}

PacketDropper::PacketDropper (double overall_drop_rate,
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
    high_freq_index (0),
    average_drop_per_event (0.0),
    event_probability (0.0),
    m_dropMode (DROP_MODE_LOSSRATE),
    m_dropModeName ("lossrate"),
    m_seqnumConfigFile ("config/config_drop_by_seqnum.txt"),
    m_timestepConfigFile ("config/config_drop_by_timestep.txt")
{
  rng.seed (44);
  dropDistribution = distribution;
  InitDistribution ();
  double avg_burst = static_cast<double> (burst_n);
  double avg_highfreq = (high_freq_min + high_freq_max) / 2.0;
  double avg_random = 1.0;
  average_drop_per_event = event_ratio_burst * avg_burst +
                           event_ratio_highfreq * avg_highfreq +
                           event_ratio_random * avg_random;
  event_probability = (average_drop_per_event > 0.0) ? (overall_drop_rate / average_drop_per_event) : 0.0;
}

void
PacketDropper::setParam (double overall_drop_rate_in,
                         int burst_n_in, int burst_offset_in,
                         int high_freq_m_in, int high_freq_min_in, int high_freq_max_in,
                         unsigned int seed)
{
  InitDistribution ();
  overall_drop_rate = overall_drop_rate_in;
  burst_n = burst_n_in;
  burst_offset = burst_offset_in;
  high_freq_m = high_freq_m_in;
  high_freq_min = high_freq_min_in;
  high_freq_max = high_freq_max_in;
  burst_remaining = 0;
  high_freq_index = 0;

  rng.seed (seed);
  double avg_burst = static_cast<double> (burst_n);
  double avg_highfreq = (high_freq_min + high_freq_max) / 2.0;
  double avg_random = 1.0;
  average_drop_per_event = event_ratio_burst * avg_burst +
                           event_ratio_highfreq * avg_highfreq +
                           event_ratio_random * avg_random;
  event_probability = (average_drop_per_event > 0.0) ? (overall_drop_rate / average_drop_per_event) : 0.0;
}

void
PacketDropper::setDropDistribution (const std::string &distribution)
{
  dropDistribution = distribution;
  InitDistribution ();
}

void
PacketDropper::SetDropMode (const std::string &mode)
{
  std::string normalized = ToLower (Trim (mode));
  if (normalized.empty () || normalized == "lossrate" || normalized == "by_lossrate" ||
      normalized == "lossrate_distribution" || normalized == "by_lossrate_and_distribution")
    {
      m_dropMode = DROP_MODE_LOSSRATE;
      m_dropModeName = "lossrate";
      return;
    }
  if (normalized == "seqnum" || normalized == "by_seqnum")
    {
      m_dropMode = DROP_MODE_SEQNUM;
      m_dropModeName = "seqnum";
      EnsureDeterministicConfigLoaded ();
      return;
    }
  if (normalized == "timestep" || normalized == "by_timestep")
    {
      m_dropMode = DROP_MODE_TIMESTEP;
      m_dropModeName = "timestep";
      EnsureDeterministicConfigLoaded ();
      return;
    }

  NS_LOG_WARN ("Unknown drop mode '" << mode << "', fallback to lossrate");
  m_dropMode = DROP_MODE_LOSSRATE;
  m_dropModeName = "lossrate";
}

std::string
PacketDropper::GetDropMode () const
{
  return m_dropModeName;
}

void
PacketDropper::SetSeqnumConfigFile (const std::string &path)
{
  std::string trimmed = Trim (path);
  if (!trimmed.empty ())
    {
      m_seqnumConfigFile = trimmed;
    }
}

void
PacketDropper::SetTimestepConfigFile (const std::string &path)
{
  std::string trimmed = Trim (path);
  if (!trimmed.empty ())
    {
      m_timestepConfigFile = trimmed;
    }
}

void
PacketDropper::ConfigurePolicy (const std::string &mode,
                                const std::string &seqnumConfigFile,
                                const std::string &timestepConfigFile)
{
  SetSeqnumConfigFile (seqnumConfigFile);
  SetTimestepConfigFile (timestepConfigFile);
  SetDropMode (mode);
}

int
PacketDropper::assertDrop (uint32_t switchId,
                           uint32_t srcNodeId,
                           uint16_t flowId,
                           uint32_t seq,
                           double nowSeconds)
{
  switch (m_dropMode)
    {
    case DROP_MODE_SEQNUM:
      EnsureDeterministicConfigLoaded ();
      return AssertDropBySeqnum (switchId, srcNodeId, flowId, seq);
    case DROP_MODE_TIMESTEP:
      EnsureDeterministicConfigLoaded ();
      return AssertDropByTimestep (switchId, srcNodeId, flowId, nowSeconds);
    case DROP_MODE_LOSSRATE:
    default:
      return AssertDropByLossrate ();
    }
}

int
PacketDropper::AssertDropByLossrate ()
{
  if (burst_remaining > 0)
    {
      --burst_remaining;
      return 3;
    }

  if (!high_freq_schedule.empty ())
    {
      bool drop = high_freq_schedule[high_freq_index++];
      if (high_freq_index >= static_cast<int> (high_freq_schedule.size ()))
        {
          high_freq_schedule.clear ();
          high_freq_index = 0;
        }
      return drop ? 2 : 0;
    }

  if (triggerEvent (event_probability))
    {
      double r = uniformReal (0.0, 1.0);
      if (r < event_ratio_burst)
        {
          int offset = uniformInt (-burst_offset, burst_offset);
          int dropCount = burst_n + offset;
          if (dropCount < 1)
            {
              dropCount = 1;
            }
          burst_remaining = dropCount - 1;
          return 3;
        }
      else if (r < event_ratio_burst + event_ratio_highfreq)
        {
          int dropCount = uniformInt (high_freq_min, high_freq_max);
          if (dropCount > high_freq_m)
            {
              dropCount = high_freq_m;
            }

          if (static_cast<int> (high_freq_schedule.size ()) != high_freq_m)
            {
              high_freq_schedule.resize (high_freq_m, false);
            }
          std::fill (high_freq_schedule.begin (), high_freq_schedule.end (), false);
          std::fill (high_freq_schedule.begin (), high_freq_schedule.begin () + dropCount, true);
          std::shuffle (high_freq_schedule.begin (), high_freq_schedule.end (), rng);

          high_freq_index = 0;
          bool drop = high_freq_schedule[high_freq_index++];
          return drop ? 2 : 0;
        }
      else
        {
          return 1;
        }
    }

  return 0;
}

int
PacketDropper::AssertDropBySeqnum (uint32_t switchId, uint32_t srcNodeId, uint16_t flowId, uint32_t seq)
{
  std::string key = MakeFlowKey (switchId, srcNodeId, flowId);
  std::unordered_map<std::string, std::unordered_map<uint32_t, uint32_t> >::iterator keyIt =
      s_seqnumDropBudget.find (key);
  if (keyIt == s_seqnumDropBudget.end ())
    {
      return 0;
    }

  std::unordered_map<uint32_t, uint32_t>::iterator seqIt = keyIt->second.find (seq);
  if (seqIt == keyIt->second.end () || seqIt->second == 0)
    {
      return 0;
    }

  --seqIt->second;
  if (seqIt->second == 0)
    {
      keyIt->second.erase (seqIt);
    }
  return 4;  // deterministic drop by sequence number
}

int
PacketDropper::AssertDropByTimestep (uint32_t switchId, uint32_t srcNodeId, uint16_t flowId, double nowSeconds)
{
  std::string key = MakeFlowKey (switchId, srcNodeId, flowId);
  std::unordered_map<std::string, TimestepState>::iterator it = s_timestepDropState.find (key);
  if (it == s_timestepDropState.end ())
    {
      return 0;
    }

  TimestepState &state = it->second;
  while (state.cursor < state.rules.size () && state.rules[state.cursor].remaining == 0)
    {
      ++state.cursor;
    }
  if (state.cursor >= state.rules.size ())
    {
      return 0;
    }

  TimestepRule &rule = state.rules[state.cursor];
  if (nowSeconds + 1e-15 < rule.startSeconds)
    {
      return 0;
    }

  --rule.remaining;
  if (rule.remaining == 0)
    {
      ++state.cursor;
    }
  return 5;  // deterministic drop by timestep window
}

void
PacketDropper::EnsureDeterministicConfigLoaded () const
{
  if (m_dropMode == DROP_MODE_SEQNUM)
    {
      LoadSeqnumConfigIfNeeded (m_seqnumConfigFile);
    }
  else if (m_dropMode == DROP_MODE_TIMESTEP)
    {
      LoadTimestepConfigIfNeeded (m_timestepConfigFile);
    }
}

void
PacketDropper::LoadSeqnumConfigIfNeeded (const std::string &path)
{
  if (s_seqnumConfigLoaded && s_seqnumConfigLoadedPath == path)
    {
      return;
    }

  s_seqnumDropBudget.clear ();
  s_seqnumConfigLoaded = true;
  s_seqnumConfigLoadedPath = path;

  std::ifstream fin (path.c_str ());
  if (!fin.is_open ())
    {
      NS_LOG_ERROR ("Failed to open seqnum drop config: " << path);
      return;
    }

  std::string raw;
  while (std::getline (fin, raw))
    {
      std::string line = Trim (StripComment (raw));
      if (line.empty ())
        {
          continue;
        }
      if (!std::isdigit (static_cast<unsigned char> (line[0])))
        {
          continue;  // skip header
        }

      size_t p1 = line.find (',');
      size_t p2 = (p1 == std::string::npos) ? std::string::npos : line.find (',', p1 + 1);
      size_t p3 = (p2 == std::string::npos) ? std::string::npos : line.find (',', p2 + 1);
      if (p1 == std::string::npos || p2 == std::string::npos || p3 == std::string::npos)
        {
          NS_LOG_WARN ("Malformed seqnum config line: " << raw);
          continue;
        }

      uint32_t switchId = 0;
      uint32_t srcNode = 0;
      uint32_t flowId = 0;
      if (!ParseScalar<uint32_t> (Trim (line.substr (0, p1)), &switchId) ||
          !ParseScalar<uint32_t> (Trim (line.substr (p1 + 1, p2 - p1 - 1)), &srcNode) ||
          !ParseScalar<uint32_t> (Trim (line.substr (p2 + 1, p3 - p2 - 1)), &flowId))
        {
          NS_LOG_WARN ("Failed to parse seqnum key fields: " << raw);
          continue;
        }

      std::string seqField = Trim (line.substr (p3 + 1));
      if (!seqField.empty () && seqField[0] == '[')
        {
          seqField.erase (seqField.begin ());
        }
      if (!seqField.empty () && seqField[seqField.size () - 1] == ']')
        {
          seqField.erase (seqField.end () - 1);
        }

      std::string key = MakeFlowKey (switchId, srcNode, static_cast<uint16_t> (flowId));
      std::stringstream ss (seqField);
      std::string item;
      while (std::getline (ss, item, ','))
        {
          item = Trim (item);
          if (item.empty ())
            {
              continue;
            }
          size_t dashPos = item.find ('-');
          if (dashPos != std::string::npos)
            {
              uint32_t left = 0;
              uint32_t right = 0;
              if (!ParseScalar<uint32_t> (Trim (item.substr (0, dashPos)), &left) ||
                  !ParseScalar<uint32_t> (Trim (item.substr (dashPos + 1)), &right))
                {
                  NS_LOG_WARN ("Failed to parse seq range '" << item << "' in line: " << raw);
                  continue;
                }
              if (left > right)
                {
                  std::swap (left, right);
                }
              for (uint32_t s = left; s <= right; ++s)
                {
                  ++s_seqnumDropBudget[key][s];
                  if (s == right)
                    {
                      break;  // avoid uint32 overflow when right == UINT32_MAX
                    }
                }
              continue;
            }
          uint32_t seq = 0;
          if (!ParseScalar<uint32_t> (item, &seq))
            {
              NS_LOG_WARN ("Failed to parse seq value '" << item << "' in line: " << raw);
              continue;
            }
          ++s_seqnumDropBudget[key][seq];
        }
    }
}

void
PacketDropper::LoadTimestepConfigIfNeeded (const std::string &path)
{
  if (s_timestepConfigLoaded && s_timestepConfigLoadedPath == path)
    {
      return;
    }

  s_timestepDropState.clear ();
  s_timestepConfigLoaded = true;
  s_timestepConfigLoadedPath = path;

  std::ifstream fin (path.c_str ());
  if (!fin.is_open ())
    {
      NS_LOG_ERROR ("Failed to open timestep drop config: " << path);
      return;
    }

  std::string raw;
  while (std::getline (fin, raw))
    {
      std::string line = Trim (StripComment (raw));
      if (line.empty ())
        {
          continue;
        }
      if (!std::isdigit (static_cast<unsigned char> (line[0])))
        {
          continue;
        }

      std::vector<std::string> fields;
      std::stringstream ss (line);
      std::string item;
      while (std::getline (ss, item, ','))
        {
          fields.push_back (Trim (item));
        }
      if (fields.size () < 5)
        {
          NS_LOG_WARN ("Malformed timestep config line: " << raw);
          continue;
        }

      uint32_t switchId = 0;
      uint32_t srcNode = 0;
      uint32_t flowId = 0;
      double startSeconds = 0.0;
      uint32_t dropNum = 0;
      if (!ParseScalar<uint32_t> (fields[0], &switchId) ||
          !ParseScalar<uint32_t> (fields[1], &srcNode) ||
          !ParseScalar<uint32_t> (fields[2], &flowId) ||
          !ParseScalar<double> (fields[3], &startSeconds) ||
          !ParseScalar<uint32_t> (fields[4], &dropNum))
        {
          NS_LOG_WARN ("Failed to parse timestep config line: " << raw);
          continue;
        }
      if (dropNum == 0)
        {
          continue;
        }

      std::string key = MakeFlowKey (switchId, srcNode, static_cast<uint16_t> (flowId));
      TimestepRule rule;
      rule.startSeconds = startSeconds;
      rule.remaining = dropNum;
      s_timestepDropState[key].rules.push_back (rule);
    }

  for (std::unordered_map<std::string, TimestepState>::iterator it = s_timestepDropState.begin ();
       it != s_timestepDropState.end (); ++it)
    {
      std::sort (it->second.rules.begin (), it->second.rules.end (),
                 [](const TimestepRule &a, const TimestepRule &b) {
                   return a.startSeconds < b.startSeconds;
                 });
      it->second.cursor = 0;
    }
}

std::string
PacketDropper::MakeFlowKey (uint32_t switchId, uint32_t srcNodeId, uint16_t flowId)
{
  std::ostringstream oss;
  oss << switchId << "," << srcNodeId << "," << flowId;
  return oss.str ();
}

std::string
PacketDropper::Trim (const std::string &s)
{
  size_t begin = s.find_first_not_of (" \t\r\n");
  if (begin == std::string::npos)
    {
      return "";
    }
  size_t end = s.find_last_not_of (" \t\r\n");
  return s.substr (begin, end - begin + 1);
}

std::string
PacketDropper::StripComment (const std::string &line)
{
  size_t pos = line.find ('#');
  if (pos == std::string::npos)
    {
      return line;
    }
  return line.substr (0, pos);
}

std::string
PacketDropper::ToLower (const std::string &s)
{
  std::string out = s;
  std::transform (out.begin (), out.end (), out.begin (),
                  [](unsigned char c) { return static_cast<char> (std::tolower (c)); });
  return out;
}

void
PacketDropper::InitDistribution ()
{
  if (dropDistribution == "amazon")
    {
      event_ratio_random = 0.53;
      event_ratio_highfreq = 0.12;
      event_ratio_burst = 0.35;
    }
  else if (dropDistribution == "google")
    {
      event_ratio_random = 0.37;
      event_ratio_highfreq = 0.51;
      event_ratio_burst = 0.12;
    }
  else if (dropDistribution == "microsoft")
    {
      event_ratio_random = 0.68;
      event_ratio_highfreq = 0.19;
      event_ratio_burst = 0.13;
    }
  else
    {
      NS_LOG_ERROR ("Unknown drop distribution: " << dropDistribution);
    }
}

bool
PacketDropper::triggerEvent (double prob)
{
  std::uniform_real_distribution<double> dist (0.0, 1.0);
  return dist (rng) < prob;
}

int
PacketDropper::uniformInt (int low, int high)
{
  int range = high - low + 1;
  return low + (rand () % range);
}

double
PacketDropper::uniformReal (double low, double high)
{
  std::uniform_real_distribution<double> dist (low, high);
  return dist (rng);
}

void
PacketDropper::PrintStatus () const
{
  std::cout << "----- PacketDropper Status -----" << std::endl;
  std::cout << "drop_mode: " << m_dropModeName << std::endl;
  std::cout << "seqnum_config_file: " << m_seqnumConfigFile << std::endl;
  std::cout << "timestep_config_file: " << m_timestepConfigFile << std::endl;
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
