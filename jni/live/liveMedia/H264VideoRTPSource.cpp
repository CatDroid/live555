/**********
This library is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 2.1 of the License, or (at your
option) any later version. (See <http://www.gnu.org/copyleft/lesser.html>.)

This library is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with this library; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
**********/
// "liveMedia"
// Copyright (c) 1996-2016 Live Networks, Inc.  All rights reserved.
// H.264 Video RTP Sources
// Implementation

#include "H264VideoRTPSource.hh"
#include "Base64.hh"

////////// H264BufferedPacket and H264BufferedPacketFactory //////////

class H264BufferedPacket: public BufferedPacket {
public:
  H264BufferedPacket(H264VideoRTPSource& ourSource);
  virtual ~H264BufferedPacket();

private: // redefined virtual functions
  virtual unsigned nextEnclosedFrameSize(unsigned char*& framePtr,
					 unsigned dataSize);
private:
  H264VideoRTPSource& fOurSource;
};

class H264BufferedPacketFactory: public BufferedPacketFactory {
private: // redefined virtual functions
  virtual BufferedPacket* createNewPacket(MultiFramedRTPSource* ourSource);
};


///////// H264VideoRTPSource implementation ////////

H264VideoRTPSource*
H264VideoRTPSource::createNew(UsageEnvironment& env, Groupsock* RTPgs,
			      unsigned char rtpPayloadFormat,
			      unsigned rtpTimestampFrequency) {
  return new H264VideoRTPSource(env, RTPgs, rtpPayloadFormat,
				rtpTimestampFrequency);
}

H264VideoRTPSource
::H264VideoRTPSource(UsageEnvironment& env, Groupsock* RTPgs,
		     unsigned char rtpPayloadFormat,
		     unsigned rtpTimestampFrequency)
  : MultiFramedRTPSource(env, RTPgs, rtpPayloadFormat, rtpTimestampFrequency,
			 new H264BufferedPacketFactory) {
}

H264VideoRTPSource::~H264VideoRTPSource() {
}

Boolean H264VideoRTPSource
::processSpecialHeader(BufferedPacket* packet,
                       unsigned& resultSpecialHeaderSize) {
  unsigned char* headerStart = packet->data(); // 这个RTP包的数据 跳过了RTP固定包头
  unsigned packetSize = packet->dataSize();
  unsigned numBytesToSkip;

  // Check the 'nal_unit_type' for special 'aggregation' or 'fragmentation' packets:
  if (packetSize < 1) return False;

  /* 对于H264 over RTP来说
  	RTP包的数据 第一个字节是NAL type 
	a.H264定义的 07(sps) 08(pps) 06(sei) 05(idr) 01(41/01)P/slice 
  	b.RTP定义的 24~31 都是H264 NALU类型 未指定的
   		 
	1-23  NAL单元  单个 NAL 单元包.
	24	  STAP-A   单一时间的组合包 (sps和pps会封装在一起)
	25	  STAP-B   单一时间的组合包
	26	  MTAP16   多个时间的组合包
	27	  MTAP24   多个时间的组合包
	28	  FU-A	   分片的单元		(H264)
	29	  FU-B	   分片的单元
    
   */
  fCurPacketNALUnitType = (headerStart[0]&0x1F);
  switch (fCurPacketNALUnitType) {
  case 24: { // STAP-A
    numBytesToSkip = 1; // discard the type byte
    break; // 跳过RTP包数据的第一个字节
  }
  case 25: case 26: case 27: { // STAP-B, MTAP16, or MTAP24
    numBytesToSkip = 3; // discard the type byte, and the initial DON
    break;
  }
  case 28: case 29: { //  FU-A or FU-B 分片的单元 比如I帧可能被分片 28
    // For these NALUs, the first two bytes are the FU indicator and the FU header.
    // If the start bit is set, we reconstruct the original NAL header into byte 1:
    if (packetSize < 2) return False;
    unsigned char startBit = headerStart[1]&0x80;
    unsigned char endBit = headerStart[1]&0x40;
    if (startBit) {
      fCurrentPacketBeginsFrame = True; // 分片单元 特殊头部 第二个字节 0x80 

      headerStart[1] = (headerStart[0]&0xE0)|(headerStart[1]&0x1F);
      numBytesToSkip = 1;
    } else {
      // The start bit is not set, so we skip both the FU indicator and header:
      fCurrentPacketBeginsFrame = False;
      numBytesToSkip = 2;
    }					// 分片单元 特殊头部 第二个字节 0x80 	
    fCurrentPacketCompletesFrame = (endBit != 0); // 标记接收包 已经完毕 收到一个RTP包的M位为 1
    break;
    /*
	如果分片的话  会把NAL拆在两个字节中 
	header[1] = (byte) (header[4] & 0x1F);  原来NALU第一个字节的0x1F
	header[1] += 0x80;
	header[0] = (byte) ((header[4] & 0x60) & 0xFF); 原来NALU第一个字节的0x60
	header[0] += 28;
	而且第二个字节0x80 代表 开始 
	第二个字节0x40 代表 结束 

    */
  }
  default: { // 一个RTP包仅由一个完整的NALU组成
    // This packet contains one complete NAL unit:
    fCurrentPacketBeginsFrame = fCurrentPacketCompletesFrame = True;
    numBytesToSkip = 0;
    break;
  }
  }

  // RTP包数据部分的特殊头部(对于H264是NAL)
  resultSpecialHeaderSize = numBytesToSkip;
  return True;
}

char const* H264VideoRTPSource::MIMEtype() const {
  return "video/H264";
}

SPropRecord* parseSPropParameterSets(char const* sPropParameterSetsStr,
                                     // result parameter:
                                     unsigned& numSPropRecords) {
  // Make a copy of the input string, so we can replace the commas with '\0's:
  char* inStr = strDup(sPropParameterSetsStr);
  if (inStr == NULL) {
    numSPropRecords = 0;
    return NULL;
  }

  // Count the number of commas (and thus the number of parameter sets):
  numSPropRecords = 1;
  char* s;
  for (s = inStr; *s != '\0'; ++s) {
    if (*s == ',') {
      ++numSPropRecords;
      *s = '\0';
    }
  }

  // Allocate and fill in the result array:
  SPropRecord* resultArray = new SPropRecord[numSPropRecords];
  s = inStr;
  for (unsigned i = 0; i < numSPropRecords; ++i) {
    resultArray[i].sPropBytes = base64Decode(s, resultArray[i].sPropLength);
    s += strlen(s) + 1;
  }

  delete[] inStr;
  return resultArray;
}


////////// H264BufferedPacket and H264BufferedPacketFactory implementation //////////

H264BufferedPacket::H264BufferedPacket(H264VideoRTPSource& ourSource)
  : fOurSource(ourSource) {
}

H264BufferedPacket::~H264BufferedPacket() {
}

unsigned H264BufferedPacket
::nextEnclosedFrameSize(unsigned char*& framePtr, unsigned dataSize) {
  unsigned resultNALUSize = 0; // if an error occurs

  switch (fOurSource.fCurPacketNALUnitType) {
  case 24: case 25: { // STAP-A or STAP-B
    // The first two bytes are NALU size:
    if (dataSize < 2) break;
    resultNALUSize = (framePtr[0]<<8)|framePtr[1];
    framePtr += 2;
    break;
  }
  case 26: { // MTAP16
    // The first two bytes are NALU size.  The next three are the DOND and TS offset:
    if (dataSize < 5) break;
    resultNALUSize = (framePtr[0]<<8)|framePtr[1];
    framePtr += 5;
    break;
  }
  case 27: { // MTAP24
    // The first two bytes are NALU size.  The next four are the DOND and TS offset:
    if (dataSize < 6) break;
    resultNALUSize = (framePtr[0]<<8)|framePtr[1];
    framePtr += 6;
    break;
  }
  default: {
    // Common case: We use the entire packet data:
    return dataSize;
  }
  }

  return (resultNALUSize <= dataSize) ? resultNALUSize : dataSize;
}

BufferedPacket* H264BufferedPacketFactory
::createNewPacket(MultiFramedRTPSource* ourSource) {
  return new H264BufferedPacket((H264VideoRTPSource&)(*ourSource));
}
