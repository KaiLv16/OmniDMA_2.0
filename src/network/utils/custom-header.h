/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2005 INRIA
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 */

#ifndef CUSTOM_HEADER_H
#define CUSTOM_HEADER_H

#include "ns3/header.h"
#include "ns3/int-header.h"

namespace ns3 {
/**
 * \ingroup ipv4
 *
 * \brief Custom packet header
 */
class CustomHeader : public Header 
{
public:
  /**
   * \brief Construct a null custom header
   */
  CustomHeader ();
  CustomHeader (uint32_t _headerType);
  /**
   * \enum EcnType
   * \brief ECN Type defined in \RFC{3168}
   */
  enum EcnType
    {
      // Prefixed with "ECN" to avoid name clash (bug 1723)
      ECN_NotECT = 0x00,
      ECN_ECT1 = 0x01,
      ECN_ECT0 = 0x02,
      ECN_CE = 0x03
    }; 
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);
  virtual TypeId GetInstanceTypeId (void) const;
  virtual void Print (std::ostream &os) const;
  virtual uint32_t GetSerializedSize (void) const;
  virtual void Serialize (Buffer::Iterator start) const;
  virtual uint32_t Deserialize (Buffer::Iterator start);

  uint32_t brief, headerType, getInt;
  enum HeaderType{
	L2_Header = 1,
	L3_Header = 2,
	L4_Header = 4
  };

  // ppp header
  uint16_t pppProto;

  // IPv4 header
  enum FlagsE {
    DONT_FRAGMENT = (1<<0),
    MORE_FRAGMENTS = (1<<1)
  };
  uint16_t m_payloadSize; //!< payload size
  uint16_t ipid; //!< identification
  uint32_t m_tos : 8; //!< TOS, also used as DSCP + ECN value
  uint32_t m_ttl : 8; //!< TTL
  uint32_t l3Prot: 8;  //!< Protocol
  uint32_t ipv4Flags : 3; //!< flags
  uint16_t m_fragmentOffset;  //!< Fragment offset
  uint32_t sip; //!< source address
  uint32_t dip; //!< destination address
  uint16_t m_checksum; //!< checksum
  uint16_t m_headerSize; //!< IP header size

  union {
	  struct {
		  uint16_t sport;        //!< Source port
		  uint16_t dport;   //!< Destination port
		  uint32_t seq;  //!< Sequence number
		  uint32_t ack;       //!< ACK number
		  uint8_t length;             //!< Length (really a uint4_t) in words.
		  uint8_t tcpFlags;              //!< Flags (really a uint6_t)
		  uint16_t windowSize;        //!< Window size
		  uint16_t urgentPointer;     //!< Urgent pointer
		  uint8_t optionBuf[32]; // buffer for storing raw options
	  } tcp;
	  struct {
		  uint16_t sport;        //!< Source port
		  uint16_t dport;   //!< Destination port
		  uint16_t payload_size;
		  // SeqTsHeader
		  uint16_t pg;
      uint16_t flow_id;
      uint16_t omni_type;
		  uint32_t seq;
		  IntHeader ih;
      uint32_t adamap_id;
	  } udp;
    struct {
		  uint16_t sport;        //!< Source port
		  uint16_t dport;   //!< Destination port
		  uint16_t payload_size;
		  // SeqTsHeader
		  uint16_t pg;
      uint16_t flow_id;
      uint16_t omni_type;
		  uint32_t seq;
		  IntHeader ih;
      uint32_t adamap_id;
	  } omni_udp;    // 用在重传包中，用于承载adamap_id
	  // CnHeader
	  struct {
		  uint16_t fid;
		  uint8_t qIndex;
		  uint16_t qfb;
		  uint8_t ecnBits;
		  uint16_t total;
	  } cnp;
	  // qbbHeader
	  struct {
		  uint16_t sport, dport;
		  uint16_t flags;
		  uint16_t pg;
      uint16_t flow_id;
      uint16_t omni_type;
		  uint32_t seq; // the qbb sequence number.
		  IntHeader ih;
		  uint32_t irnNack;
		  uint16_t irnNackSize;

      uint32_t omniDMAAdamapId;
      uint64_t omniDMAAdamapBitmap[4];
      uint32_t omniDMAAdamapStartSeq;
      uint32_t omniDMAAdamapReprLength;
      uint32_t omniDMATableIndex;
      uint32_t omniDMACumAckSeq;
	  } ack;
	  // PauseHeader
	  struct {
		  uint32_t time;
		  uint32_t qlen;
		  uint8_t qIndex;
	  } pfc;
  };

  uint8_t GetIpv4EcnBits (void) const;
  static uint32_t GetAckSerializedSize(void);
  static uint32_t GetUdpHeaderSize(void); // include udp, seqTs, INT
  static uint32_t GetOmniUdpHeaderSize(void); // include udp, seqTs, INT, adamap_id
  static uint32_t GetStaticWholeHeaderSize(void); // ppp + ip + udp + int

  void SetOmniDMAAdamapId(uint32_t id);
  void SetOmniDMAAdamapBitmap(uint64_t bitmap);
  void SetOmniDMAAdamapStartSeq(uint32_t startSeq);
  void SetOmniDMAAdamapReprLength(uint32_t reprLength);
  void SetOmniDMATableIndex(uint32_t Index);

  uint32_t GetOmniDMAAdamapId() const;
  uint64_t GetOmniDMAAdamapBitmap() const;
  uint32_t GetOmniDMAAdamapStartSeq() const;
  uint32_t GetOmniDMAAdamapReprLength() const;
  uint32_t GetOmniDMATableIndex() const;
};

} // namespace ns3


#endif /* CUSTOM_HEADER_H */
