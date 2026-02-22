//yibo

#ifndef QBB_HEADER_H
#define QBB_HEADER_H

#include <stdint.h>
#include "ns3/header.h"
#include "ns3/buffer.h"
#include "ns3/int-header.h"
#include "ns3/adamap.h"

namespace ns3 {

/**
 * \ingroup Pause
 * \brief Header for the Congestion Notification Message
 *
 * This class has two fields: The five-tuple flow id and the quantized
 * congestion level. This can be serialized to or deserialzed from a byte
 * buffer.
 */
 
class qbbHeader : public Header
{
public:
 
  enum {
	  FLAG_CNP = 0
  };
  qbbHeader (uint16_t pg);
  qbbHeader ();
  virtual ~qbbHeader ();

//Setters
  /**
   * \param pg The PG
   */
  void SetPG (uint16_t pg);
  void SetOmniType (uint16_t type = 10000);
  void SetFlowId (uint16_t Id);
  void SetSeq(uint32_t seq);
  void SetSport(uint32_t _sport);
  void SetDport(uint32_t _dport);
  void SetTs(uint64_t ts);
  void SetCnp();
  void SetIntHeader(const IntHeader &_ih);
  void SetIrnNack(uint32_t seq);
  void SetIrnNackSize(size_t sz);

  void SetOmniDMAAdamapId(uint32_t id);
  void SetOmniDMAAdamapBitmap(uint64_t bitmap);
  void SetOmniDMAAdamapStartSeq(uint32_t startSeq);
  void SetOmniDMAAdamapReprLength(uint32_t reprLength);
  void SetOmniDMATableIndex(uint32_t Index);
  void SetOmniDMACumAckSeq(uint32_t seq);

//Getters
  /**
   * \return The pg
   */
  uint16_t GetPG () const;
  uint16_t GetOmniType () const;
  uint16_t GetFlowId () const;
  uint32_t GetSeq() const;
  uint16_t GetPort() const;
  uint16_t GetSport() const;
  uint16_t GetDport() const;
  uint64_t GetTs() const;
  uint8_t GetCnp() const;
  uint32_t GetIrnNack() const;
  size_t GetIrnNackSize() const;

  uint32_t GetOmniDMAAdamapId() const;
  uint64_t GetOmniDMAAdamapBitmap() const;
  uint32_t GetOmniDMAAdamapStartSeq() const;
  uint32_t GetOmniDMAAdamapReprLength() const;
  uint32_t GetOmniDMATableIndex() const;
  uint32_t GetOmniDMACumAckSeq() const;

  static TypeId GetTypeId (void);
  virtual TypeId GetInstanceTypeId (void) const;
  virtual void Print (std::ostream &os) const;
  virtual uint32_t GetSerializedSize (void) const;
  virtual void Serialize (Buffer::Iterator start) const;
  virtual uint32_t Deserialize (Buffer::Iterator start);
  static uint32_t GetBaseSize(); // size without INT

private:
  uint16_t sport, dport;
  uint16_t flags;
  uint16_t m_pg;
  uint16_t m_flow_id;
  uint16_t m_omni_type;
  uint32_t m_seq; // the qbb sequence number.
  IntHeader ih;
  uint32_t m_irn_nack;
  uint16_t m_irn_nack_size;
  bool enable_irn;

  uint32_t omniDMAAdamapId;
  uint64_t omniDMAAdamapBitmap;
  uint32_t omniDMAAdamapStartSeq;
  uint32_t omniDMAAdamapReprLength;
  uint32_t omniDMATableIndex;
  uint32_t omniDMACumAckSeq;
};

}; // namespace ns3

#endif /* QBB_HEADER */
