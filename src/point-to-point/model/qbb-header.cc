#include <stdint.h>
#include <iostream>
#include "qbb-header.h"
#include "ns3/buffer.h"
#include "ns3/address-utils.h"
#include "ns3/log.h"

NS_LOG_COMPONENT_DEFINE("qbbHeader");

namespace ns3 {

	NS_OBJECT_ENSURE_REGISTERED(qbbHeader);

	qbbHeader::qbbHeader(uint16_t pg)
		: m_pg(pg), sport(0), dport(0), flags(0), m_seq(0)
	{
	}

	qbbHeader::qbbHeader()
		: m_pg(0), sport(0), dport(0), flags(0), m_seq(0)
	{}

	qbbHeader::~qbbHeader()
	{}

	void qbbHeader::SetPG(uint16_t pg)
	{
		m_pg = pg;
	}
	void qbbHeader::SetFlowId(uint16_t Id)
	{
		m_flow_id = Id;
	}

	// 设置为10000，表示不是OmniDMA的包
	void qbbHeader::SetOmniType(uint16_t type)
	{
		m_omni_type = type;
	}

	void qbbHeader::SetSeq(uint32_t seq)
	{
		m_seq = seq;
	}

	void qbbHeader::SetSport(uint32_t _sport){
		sport = _sport;
	}
	void qbbHeader::SetDport(uint32_t _dport){
		dport = _dport;
	}

	void qbbHeader::SetTs(uint64_t ts){
		NS_ASSERT_MSG(IntHeader::mode == 1, "qbbHeader cannot SetTs when IntHeader::mode != 1");
		ih.ts = ts;
	}
	void qbbHeader::SetCnp(){
		flags |= 1 << FLAG_CNP;
	}
	void qbbHeader::SetIntHeader(const IntHeader &_ih){
		ih = _ih;
	}
	void qbbHeader::SetIrnNack(uint32_t seq){
		m_irn_nack = seq;
	}
	void qbbHeader::SetIrnNackSize(size_t sz){
		m_irn_nack_size = (uint16_t)sz;
	}


	void qbbHeader::SetOmniDMAAdamapId(uint32_t id){
		omniDMAAdamapId = id;
	}
	void qbbHeader::SetOmniDMAAdamapBitmap(uint64_t bitmap) {
		omniDMAAdamapBitmap = bitmap;
	}
	void qbbHeader::SetOmniDMAAdamapStartSeq(uint32_t startSeq){
		omniDMAAdamapStartSeq = startSeq;
	}
	void qbbHeader::SetOmniDMAAdamapReprLength(uint32_t reprLength){
		omniDMAAdamapReprLength = reprLength;
	}
	void qbbHeader::SetOmniDMATableIndex(uint32_t Index){
		omniDMATableIndex = Index;
	}



	uint16_t qbbHeader::GetPG() const
	{
		return m_pg;
	}
	
	uint16_t qbbHeader::GetFlowId() const
	{
		return m_flow_id;
	}

	uint16_t qbbHeader::GetOmniType() const
	{
		return m_omni_type;
	}

	uint32_t qbbHeader::GetSeq() const
	{
		return m_seq;
	}

	uint16_t qbbHeader::GetSport() const{
		return sport;
	}
	uint16_t qbbHeader::GetDport() const{
		return dport;
	}

	uint64_t qbbHeader::GetTs() const {
		NS_ASSERT_MSG(IntHeader::mode == 1, "qbbHeader cannot GetTs when IntHeader::mode != 1");
		return ih.ts;
	}
	uint8_t qbbHeader::GetCnp() const{
		return (flags >> FLAG_CNP) & 1;
	}
	uint32_t qbbHeader::GetIrnNack() const{
		return m_irn_nack;
	}
	size_t qbbHeader::GetIrnNackSize() const{
		return (size_t) m_irn_nack_size;
	}

	
	uint32_t qbbHeader::GetOmniDMAAdamapId() const{
		return omniDMAAdamapId;
	}
	uint64_t qbbHeader::GetOmniDMAAdamapBitmap() const{
		return omniDMAAdamapBitmap;
	}
	uint32_t qbbHeader::GetOmniDMAAdamapStartSeq() const{
		return omniDMAAdamapStartSeq;
	}
	uint32_t qbbHeader::GetOmniDMAAdamapReprLength() const{
		return omniDMAAdamapReprLength;
	}
	uint32_t qbbHeader::GetOmniDMATableIndex() const{
		return omniDMATableIndex;
	}

	TypeId
		qbbHeader::GetTypeId(void)
	{
		static TypeId tid = TypeId("ns3::qbbHeader")
			.SetParent<Header>()
			.AddConstructor<qbbHeader>()
			;
		return tid;
	}
	TypeId
		qbbHeader::GetInstanceTypeId(void) const
	{
		return GetTypeId();
	}
	void qbbHeader::Print(std::ostream &os) const
	{
		os << "qbb:" << "pg=" << m_pg << "flow_id=" << m_flow_id <<",omniType=" << m_omni_type << ",seq=" << m_seq
		<< "\n,AdamapId=" << omniDMAAdamapId << ",AdamapBitmap=" << omniDMAAdamapBitmap
		<< "\n,StartSeq=" << omniDMAAdamapStartSeq << ",ReprLength=" << omniDMAAdamapReprLength << ",TableIndex=" << omniDMATableIndex;
	}
	uint32_t qbbHeader::GetSerializedSize(void)  const
	{
		return GetBaseSize() + IntHeader::GetStaticSize();
	}
	uint32_t qbbHeader::GetBaseSize() {
		qbbHeader tmp;
		return sizeof(tmp.sport) + sizeof(tmp.dport) + sizeof(tmp.flags) + sizeof(tmp.m_pg) 
		+ sizeof(tmp.m_flow_id) + sizeof(tmp.m_omni_type) + sizeof(tmp.m_seq) + sizeof(tmp.m_irn_nack) + sizeof(tmp.m_irn_nack_size)
		+sizeof(tmp.omniDMAAdamapId) + sizeof(tmp.omniDMAAdamapBitmap) + sizeof(tmp.omniDMAAdamapStartSeq) + sizeof(tmp.omniDMAAdamapReprLength) + sizeof(tmp.omniDMATableIndex);
	}
	void qbbHeader::Serialize(Buffer::Iterator start)  const
	{
		Buffer::Iterator i = start;
		i.WriteU16(sport);
		i.WriteU16(dport);
		i.WriteU16(flags);
		i.WriteU16(m_pg);
		i.WriteU16(m_flow_id);
		i.WriteU16(m_omni_type);
		i.WriteU32(m_seq);
		i.WriteU32(m_irn_nack);
		i.WriteU16(m_irn_nack_size);
		i.WriteU32(omniDMAAdamapId);
		i.WriteU64(omniDMAAdamapBitmap);
		i.WriteU32(omniDMAAdamapStartSeq);
		i.WriteU32(omniDMAAdamapReprLength);
		i.WriteU32(omniDMATableIndex);

		// write IntHeader
		ih.Serialize(i);
	}

	uint32_t qbbHeader::Deserialize(Buffer::Iterator start)
	{
		Buffer::Iterator i = start;
		sport = i.ReadU16();
		dport = i.ReadU16();
		flags = i.ReadU16();
		m_pg = i.ReadU16();
		m_flow_id = i.ReadU16();
		m_omni_type = i.ReadU16();
		m_seq = i.ReadU32();
		m_irn_nack = i.ReadU32();
		m_irn_nack_size = i.ReadU16();
		omniDMAAdamapId = i.ReadU32();
		omniDMAAdamapBitmap = i.ReadU64();
		omniDMAAdamapStartSeq = i.ReadU32();
		omniDMAAdamapReprLength = i.ReadU32();
		omniDMATableIndex = i.ReadU32();

		// read IntHeader
		ih.Deserialize(i);
		return GetSerializedSize();
	}
}; // namespace ns3
