/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2009 INRIA
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

 #include "ns3/assert.h"
 #include "ns3/log.h"
 #include "ns3/header.h"
 #include "ns3/simulator.h"
 #include "seq-omni-ts-header.h"
 
 NS_LOG_COMPONENT_DEFINE ("SeqOmniTsHeader");
 
 namespace ns3 {
 
 NS_OBJECT_ENSURE_REGISTERED (SeqOmniTsHeader);
 
 SeqOmniTsHeader::SeqOmniTsHeader ()
   : m_seq (0)
 {
   if (IntHeader::mode == 1)
     ih.ts = Simulator::Now().GetTimeStep();
 }
 
 void
 SeqOmniTsHeader::SetSeq (uint32_t seq)
 {
   m_seq = seq;
 }
 uint32_t
 SeqOmniTsHeader::GetSeq (void) const
 {
   return m_seq;
 }
 
 void
 SeqOmniTsHeader::SetPG (uint16_t pg)
 {
   m_pg = pg;
 }
 uint16_t
 SeqOmniTsHeader::GetPG (void) const
 {
   return m_pg;
 }

 void
 SeqOmniTsHeader::SetOmniType (uint16_t type)
 {
   m_omni_type = type;
 }
 uint16_t
 SeqOmniTsHeader::GetOmniType (void) const
 {
   return m_omni_type;
 }
 
 void
 SeqOmniTsHeader::SetFlowId (uint16_t Id)
 {
   m_flow_id = Id;
 }
 uint16_t
 SeqOmniTsHeader::GetFlowId (void) const
 {
   return m_flow_id;
 }
 
 void SeqOmniTsHeader::SetAdamapId (uint16_t id){
   m_adamap_id = id;
 }
 uint16_t SeqOmniTsHeader::GetAdamapId () const{
    return m_adamap_id;
 }

 Time
 SeqOmniTsHeader::GetTs (void) const
 {
   NS_ASSERT_MSG(IntHeader::mode == 1, "SeqOmniTsHeader cannot GetTs when IntHeader::mode != 1");
   return TimeStep (ih.ts);
 }
 
 TypeId
 SeqOmniTsHeader::GetTypeId (void)
 {
   static TypeId tid = TypeId ("ns3::SeqOmniTsHeader")
     .SetParent<Header> ()
     .AddConstructor<SeqOmniTsHeader> ()
   ;
   return tid;
 }
 TypeId
 SeqOmniTsHeader::GetInstanceTypeId (void) const
 {
   return GetTypeId ();
 }
 void
 SeqOmniTsHeader::Print (std::ostream &os) const
 {
   //os << "(seq=" << m_seq << " time=" << TimeStep (m_ts).GetSeconds () << ")";
   //os << m_seq << " " << TimeStep (m_ts).GetSeconds () << " " << m_pg;
   os << m_seq << " " << m_pg;
 }
 uint32_t
 SeqOmniTsHeader::GetSerializedSize (void) const
 {
   return GetHeaderSize();
 }

 uint32_t SeqOmniTsHeader::GetHeaderSize(void){
   return 12 + IntHeader::GetStaticSize();
 }
 
 void
 SeqOmniTsHeader::Serialize (Buffer::Iterator start) const
 {
   Buffer::Iterator i = start;
   i.WriteHtonU32 (m_seq);
   i.WriteHtonU16 (m_omni_type);
   i.WriteHtonU16 (m_flow_id);
   i.WriteHtonU16 (m_pg);
 
   // write IntHeader
   ih.Serialize(i);
   i.WriteHtonU16 (m_adamap_id);
 }
 uint32_t
 SeqOmniTsHeader::Deserialize (Buffer::Iterator start)
 {
   Buffer::Iterator i = start;
   m_seq = i.ReadNtohU32 ();
   m_omni_type = i.ReadNtohU16 ();
   m_flow_id = i.ReadNtohU16 ();
   m_pg = i.ReadNtohU16 ();
 
   // read IntHeader
   ih.Deserialize(i);
   m_adamap_id = i.ReadNtohU16 ();
   return GetSerializedSize ();
 }
 
 } // namespace ns3
 