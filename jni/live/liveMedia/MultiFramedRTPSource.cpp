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
// RTP source for a common kind of payload format: Those that pack multiple,
// complete codec frames (as many as possible) into each RTP packet.
// Implementation

#include "MultiFramedRTPSource.hh"
#include "RTCP.hh"
#include "GroupsockHelper.hh"
#include <string.h>

////////// ReorderingPacketBuffer definition //////////

class ReorderingPacketBuffer {
public:
  ReorderingPacketBuffer(BufferedPacketFactory* packetFactory);
  virtual ~ReorderingPacketBuffer();
  void reset();

  BufferedPacket* getFreePacket(MultiFramedRTPSource* ourSource);
  Boolean storePacket(BufferedPacket* bPacket);
  BufferedPacket* getNextCompletedPacket(Boolean& packetLossPreceded);
  void releaseUsedPacket(BufferedPacket* packet);
  void freePacket(BufferedPacket* packet) {
    if (packet != fSavedPacket) {
      delete packet;
    } else {
      fSavedPacketFree = True;
    }
  }
  Boolean isEmpty() const { return fHeadPacket == NULL; }

  void setThresholdTime(unsigned uSeconds) { fThresholdTime = uSeconds; }
  void resetHaveSeenFirstPacket() { fHaveSeenFirstPacket = False; }

private:
  BufferedPacketFactory* fPacketFactory;
  unsigned fThresholdTime; // uSeconds
  Boolean fHaveSeenFirstPacket; // used to set initial "fNextExpectedSeqNo"
  unsigned short fNextExpectedSeqNo;
  BufferedPacket* fHeadPacket;
  BufferedPacket* fTailPacket;	// 指向最后一个接收到的包
  BufferedPacket* fSavedPacket;
      // to avoid calling new/free in the common case
  Boolean fSavedPacketFree;
};


////////// MultiFramedRTPSource implementation //////////

MultiFramedRTPSource
::MultiFramedRTPSource(UsageEnvironment& env, Groupsock* RTPgs,
		       unsigned char rtpPayloadFormat,
		       unsigned rtpTimestampFrequency,
		       BufferedPacketFactory* packetFactory)
  : RTPSource(env, RTPgs, rtpPayloadFormat, rtpTimestampFrequency) {
  reset();
  fReorderingBuffer = new ReorderingPacketBuffer(packetFactory);

  // Try to use a big receive buffer for RTP:
  increaseReceiveBufferTo(env, RTPgs->socketNum(), 50*1024);
}

void MultiFramedRTPSource::reset() {
  fCurrentPacketBeginsFrame = True; // by default
  fCurrentPacketCompletesFrame = True; // by default
  fAreDoingNetworkReads = False;
  fPacketReadInProgress = NULL;
  fNeedDelivery = False;
  fPacketLossInFragmentedFrame = False;
}

MultiFramedRTPSource::~MultiFramedRTPSource() {
  delete fReorderingBuffer;
}

Boolean MultiFramedRTPSource
::processSpecialHeader(BufferedPacket* /*packet*/,
		       unsigned& resultSpecialHeaderSize) {
  // Default implementation: Assume no special header:
  resultSpecialHeaderSize = 0;
  return True;
}

Boolean MultiFramedRTPSource
::packetIsUsableInJitterCalculation(unsigned char* /*packet*/,
				    unsigned /*packetSize*/) {
  // Default implementation:
  return True;
}

void MultiFramedRTPSource::doStopGettingFrames() {
  if (fPacketReadInProgress != NULL) {
    fReorderingBuffer->freePacket(fPacketReadInProgress);
    fPacketReadInProgress = NULL;
  }
  envir().taskScheduler().unscheduleDelayedTask(nextTask());
  fRTPInterface.stopNetworkReading();
  fReorderingBuffer->reset();
  reset();
}

void MultiFramedRTPSource::doGetNextFrame() {
  if (!fAreDoingNetworkReads) {
    // 启动背景包接收处理 
    // 无论是TCP还是UDP最后RTP包都到这里 MultiFramedRTPSource::networkReadHandler
    // schdule只是在select有读数据的时候 回调这个接口 并不是读取完了数据 回调
    // 在 MultiFramedRTPSource::networkReadHandler 才会调用 RTPInterface去读取一个RTP包
    // Turn on background read handling of incoming packets:
    fAreDoingNetworkReads = True;
    TaskScheduler::BackgroundHandlerProc* handler
      = (TaskScheduler::BackgroundHandlerProc*)&networkReadHandler; 
    fRTPInterface.startNetworkReading(handler);
  }

  fSavedTo = fTo;
  fSavedMaxSize = fMaxSize;
  fFrameSize = 0; // for now  开始接收下一帧  如果最后fFrameSize==0 不会回调通知客户
  fNeedDelivery = True;
  doGetNextFrame1();
}

void MultiFramedRTPSource::doGetNextFrame1() {
  while (fNeedDelivery) {
    // If we already have packet data available, then deliver it now.
    Boolean packetLossPrecededThis;
    BufferedPacket* nextPacket
      = fReorderingBuffer->getNextCompletedPacket(packetLossPrecededThis);
    if (nextPacket == NULL) break;

    fNeedDelivery = False;

    if (nextPacket->useCount() == 0) {
      // Before using the packet, check whether it has a special header
      // that needs to be processed:
      unsigned specialHeaderSize;
      if (!processSpecialHeader(nextPacket, specialHeaderSize)) {
	// Something's wrong with the header; reject the packet:
	fReorderingBuffer->releaseUsedPacket(nextPacket);
	fNeedDelivery = True;
	continue;
      }
      nextPacket->skip(specialHeaderSize);
    }

    // Check whether we're part of a multi-packet frame, and whether
    // there was packet loss that would render this packet unusable:
    // 检查我们是否 多包帧 的一部分  和 是否有包丢了  有包丢了的话 就会使这个包无效
    if (fCurrentPacketBeginsFrame) {
      if (packetLossPrecededThis || fPacketLossInFragmentedFrame) {
	// We didn't get all of the previous frame.
	// Forget any data that we used from it:
	// 不能获取上一帧的 所有packet fFrameSize = 0 那么就不会回调通知客户端
	fTo = fSavedTo; fMaxSize = fSavedMaxSize;
	fFrameSize = 0;
      }
      fPacketLossInFragmentedFrame = False;
    } else if (packetLossPrecededThis) {
      // We're in a multi-packet frame, with preceding packet loss
      fPacketLossInFragmentedFrame = True;
    }
    if (fPacketLossInFragmentedFrame) {
      // This packet is unusable; reject it:
      fReorderingBuffer->releaseUsedPacket(nextPacket);
      fNeedDelivery = True;
      continue;
    }

    // The packet is usable. Deliver all or part of it to our caller:
    unsigned frameSize;
    nextPacket->use(fTo, fMaxSize, frameSize, fNumTruncatedBytes,
		    fCurPacketRTPSeqNum, fCurPacketRTPTimestamp,
		    fPresentationTime, fCurPacketHasBeenSynchronizedUsingRTCP,
		    fCurPacketMarkerBit);
    // fPresentationTime 是绝对时间  是一开始的时间+(时间戳的差值)+()+()...
    // fCurPacketRTPTimestamp 是rtp header的时间戳 需要除以90000
    fFrameSize += frameSize;

    if (!nextPacket->hasUsableData()) {
      // We're completely done with this packet now
      fReorderingBuffer->releaseUsedPacket(nextPacket);
    }

    if (fCurrentPacketCompletesFrame && fFrameSize > 0) { // 如果这一帧 不完整  就会 fFrameSize=0 不通知客户端
      // We have all the data that the client wants.
      if (fNumTruncatedBytes > 0) {
	envir() << "MultiFramedRTPSource::doGetNextFrame1(): The total received frame size exceeds the client's buffer size ("
		<< fSavedMaxSize << ").  "
		<< fNumTruncatedBytes << " bytes of trailing data will be dropped!\n";
      }

      // 一般情况 没有更多的 在队列中等待进来的包 所以直接调用afterGetting
      // 特殊情况 使用EventLoop 调用afterGetting 
      
      // Call our own 'after getting' function, so that the downstream object can consume the data:
      if (fReorderingBuffer->isEmpty()) {
	// Common case optimization: There are no more queued incoming packets, so this code will not get
	// executed again without having first returned to the event loop.  Call our 'after getting' function
	// directly, because there's no risk of a long chain of recursion (and thus stack overflow):
	afterGetting(this);
      } else {
	// Special case: Call our 'after getting' function via the event loop. 
	nextTask() = envir().taskScheduler().scheduleDelayedTask(0,
								 (TaskFunc*)FramedSource::afterGetting, this);
      }
    } else {
      // This packet contained fragmented data, and does not complete
      // the data that the client wants.  Keep getting data:
      fTo += frameSize; fMaxSize -= frameSize;
      fNeedDelivery = True;
    }
  }
}

void MultiFramedRTPSource
::setPacketReorderingThresholdTime(unsigned uSeconds) {
  fReorderingBuffer->setThresholdTime(uSeconds);
}

#define ADVANCE(n) do { bPacket->skip(n); } while (0)

// 读取到一个RTP包
void MultiFramedRTPSource::networkReadHandler(MultiFramedRTPSource* source, int /*mask*/) {
  source->networkReadHandler1();
}

void MultiFramedRTPSource::networkReadHandler1() {// 读取到一个RTP包
  BufferedPacket* bPacket = fPacketReadInProgress;
  if (bPacket == NULL) {
    // Normal case: Get a free BufferedPacket descriptor to hold the new network packet:
    bPacket = fReorderingBuffer->getFreePacket(this);
  }

  // Read the network packet, and perform sanity checks on the RTP header:
  Boolean readSuccess = False;
  do {
    struct sockaddr_in fromAddress;
    Boolean packetReadWasIncomplete = fPacketReadInProgress != NULL;
    // 如果上一次读一个RTP包 还没有完整 fPacketReadInProgress 会被赋值 
    // 一般UDP的话 一次recvfrom就会读到一个完整的RTP包
    // 但是TCP的话 可能一次recvfrom读取不到一个完整的RTP包
    if (!bPacket->fillInData(fRTPInterface, fromAddress, packetReadWasIncomplete)) {
      if (bPacket->bytesAvailable() == 0) { // should not happen??
	envir() << "MultiFramedRTPSource internal error: Hit limit when reading incoming packet over TCP\n";
      }
      fPacketReadInProgress = NULL;
      break;
    }
    if (packetReadWasIncomplete) { 
      // We need additional read(s) before we can process the incoming packet:
      fPacketReadInProgress = bPacket; 	// 代表这一次读取RTP包还不完整 下次进来networkReadHandler1就会packetReadWasIncomplete = true 
      return;				// 没有收到完整的RTP包 立刻返回
    } else {
      fPacketReadInProgress = NULL;	// 代表已经读取完毕
    }
#ifdef TEST_LOSS
    setPacketReorderingThresholdTime(0);
       // don't wait for 'lost' packets to arrive out-of-order later
    if ((our_random()%10) == 0) break; // simulate 10% packet loss
#endif

    // Check for the 12-byte RTP header:
    if (bPacket->dataSize() < 12) break;
    unsigned rtpHdr = ntohl(*(u_int32_t*)(bPacket->data())); ADVANCE(4);
    Boolean rtpMarkerBit = (rtpHdr&0x00800000) != 0;
    unsigned rtpTimestamp = ntohl(*(u_int32_t*)(bPacket->data()));ADVANCE(4);
    unsigned rtpSSRC = ntohl(*(u_int32_t*)(bPacket->data())); ADVANCE(4);

    // Check the RTP version number (it should be 2):
    if ((rtpHdr&0xC0000000) != 0x80000000) break;

    // Check the Payload Type. 检查负载类型 
    unsigned char rtpPayloadType = (unsigned char)((rtpHdr&0x007F0000)>>16);
    if (rtpPayloadType != rtpPayloadFormat()) {
      if (fRTCPInstanceForMultiplexedRTCPPackets != NULL
	  && rtpPayloadType >= 64 && rtpPayloadType <= 95) {
	// This is a multiplexed RTCP packet, and we've been asked to deliver such packets.
	// Do so now:
	fRTCPInstanceForMultiplexedRTCPPackets
	  ->injectReport(bPacket->data()-12, bPacket->dataSize()+12, fromAddress);
      }
      break;
    }

    // Skip over any CSRC identifiers in the header:
    unsigned cc = (rtpHdr>>24)&0x0F;
    if (bPacket->dataSize() < cc*4) break;
    ADVANCE(cc*4);

    // Check for (& ignore) any RTP header extension
    if (rtpHdr&0x10000000) {
      if (bPacket->dataSize() < 4) break;
      unsigned extHdr = ntohl(*(u_int32_t*)(bPacket->data())); ADVANCE(4);
      unsigned remExtSize = 4*(extHdr&0xFFFF);
      if (bPacket->dataSize() < remExtSize) break;
      ADVANCE(remExtSize);
    }

    // Discard any padding bytes:
    if (rtpHdr&0x20000000) {
      if (bPacket->dataSize() == 0) break;
      unsigned numPaddingBytes
	= (unsigned)(bPacket->data())[bPacket->dataSize()-1];
      if (bPacket->dataSize() < numPaddingBytes) break;
      bPacket->removePadding(numPaddingBytes);
    }

    // The rest of the packet is the usable data.  Record and save it:
    if (rtpSSRC != fLastReceivedSSRC) {
      // The SSRC of incoming packets has changed.  Unfortunately we don't yet handle streams that contain multiple SSRCs,
      // but we can handle a single-SSRC stream where the SSRC changes occasionally:
      fLastReceivedSSRC = rtpSSRC;
      fReorderingBuffer->resetHaveSeenFirstPacket();
    }
    unsigned short rtpSeqNo = (unsigned short)(rtpHdr&0xFFFF);
    Boolean usableInJitterCalculation
      = packetIsUsableInJitterCalculation((bPacket->data()),
						  bPacket->dataSize());
    struct timeval presentationTime; // computed by:
    Boolean hasBeenSyncedUsingRTCP; // computed by:
    receptionStatsDB()
      .noteIncomingPacket(rtpSSRC, rtpSeqNo, rtpTimestamp,
			  timestampFrequency(),
			  usableInJitterCalculation, presentationTime,
			  hasBeenSyncedUsingRTCP, bPacket->dataSize());

    // Fill in the rest of the packet descriptor, and store it:
    struct timeval timeNow;
    gettimeofday(&timeNow, NULL);
    bPacket->assignMiscParams(rtpSeqNo, rtpTimestamp, presentationTime,
			      hasBeenSyncedUsingRTCP, rtpMarkerBit,
			      timeNow);
    if (!fReorderingBuffer->storePacket(bPacket)) break;

    readSuccess = True;
  } while (0);
  if (!readSuccess) fReorderingBuffer->freePacket(bPacket);

  doGetNextFrame1();
  // If we didn't get proper data this time, we'll get another chance
}


////////// BufferedPacket and BufferedPacketFactory implementation /////

#define MAX_PACKET_SIZE 65536

BufferedPacket::BufferedPacket()
  : fPacketSize(MAX_PACKET_SIZE),
    fBuf(new unsigned char[MAX_PACKET_SIZE]), 	// 一个RTP包的最大大小 MAX_PACKET_SIZE 65536 
    fNextPacket(NULL) { 			// 所有RTP包连接在一起
}

BufferedPacket::~BufferedPacket() {
  delete fNextPacket;
  delete[] fBuf;
}

void BufferedPacket::reset() {
  fHead = fTail = 0;
  fUseCount = 0;
  fIsFirstPacket = False; // by default
}

// The following function has been deprecated:
unsigned BufferedPacket
::nextEnclosedFrameSize(unsigned char*& /*framePtr*/, unsigned dataSize) {
  // By default, use the entire buffered data, even though it may consist
  // of more than one frame, on the assumption that the client doesn't
  // care.  (This is more efficient than delivering a frame at a time)
  return dataSize;
}

void BufferedPacket
::getNextEnclosedFrameParameters(unsigned char*& framePtr, unsigned dataSize,
				 unsigned& frameSize,
				 unsigned& frameDurationInMicroseconds) {
  // By default, use the entire buffered data, even though it may consist
  // of more than one frame, on the assumption that the client doesn't
  // care.  (This is more efficient than delivering a frame at a time)

  // For backwards-compatibility with existing uses of (the now deprecated)
  // "nextEnclosedFrameSize()", call that function to implement this one:
  frameSize = nextEnclosedFrameSize(framePtr, dataSize);

  frameDurationInMicroseconds = 0; // by default.  Subclasses should correct this.
}

Boolean BufferedPacket::fillInData(RTPInterface& rtpInterface, struct sockaddr_in& fromAddress,
				   Boolean& packetReadWasIncomplete) {
  if (!packetReadWasIncomplete) reset();

  unsigned const maxBytesToRead = bytesAvailable();
  if (maxBytesToRead == 0) return False; // exceeded buffer size when reading over TCP

  unsigned numBytesRead;
  int tcpSocketNum; // not used
  unsigned char tcpStreamChannelId; // not used
  if (!rtpInterface.handleRead(&fBuf[fTail], maxBytesToRead,
			       numBytesRead, fromAddress,
			       tcpSocketNum, tcpStreamChannelId,
			       packetReadWasIncomplete)) {
    return False;
  }
  fTail += numBytesRead; // 考虑到一个RTP包可能分成几个UDP包发送
  return True;
}

void BufferedPacket
::assignMiscParams(unsigned short rtpSeqNo, unsigned rtpTimestamp,
		   struct timeval presentationTime,
		   Boolean hasBeenSyncedUsingRTCP, Boolean rtpMarkerBit,
		   struct timeval timeReceived) {
  fRTPSeqNo = rtpSeqNo;
  fRTPTimestamp = rtpTimestamp;
  fPresentationTime = presentationTime;
  fHasBeenSyncedUsingRTCP = hasBeenSyncedUsingRTCP;
  fRTPMarkerBit = rtpMarkerBit;
  fTimeReceived = timeReceived;
}

void BufferedPacket::skip(unsigned numBytes) {
  fHead += numBytes;
  if (fHead > fTail) fHead = fTail;
}

void BufferedPacket::removePadding(unsigned numBytes) {
  if (numBytes > fTail-fHead) numBytes = fTail-fHead;
  fTail -= numBytes;
}

void BufferedPacket::appendData(unsigned char* newData, unsigned numBytes) {
  if (numBytes > fPacketSize-fTail) numBytes = fPacketSize - fTail;
  memmove(&fBuf[fTail], newData, numBytes);
  fTail += numBytes;
}

void BufferedPacket::use(unsigned char* to, unsigned toSize,
			 unsigned& bytesUsed, unsigned& bytesTruncated,
			 unsigned short& rtpSeqNo, unsigned& rtpTimestamp,
			 struct timeval& presentationTime,
			 Boolean& hasBeenSyncedUsingRTCP,
			 Boolean& rtpMarkerBit) {
  unsigned char* origFramePtr = &fBuf[fHead];
  unsigned char* newFramePtr = origFramePtr; // may change in the call below
  unsigned frameSize, frameDurationInMicroseconds;
  getNextEnclosedFrameParameters(newFramePtr, fTail - fHead,
				 frameSize, frameDurationInMicroseconds);
  if (frameSize > toSize) {
    bytesTruncated += frameSize - toSize;
    bytesUsed = toSize;
  } else {
    bytesTruncated = 0;
    bytesUsed = frameSize;
  }

  memmove(to, newFramePtr, bytesUsed);
  fHead += (newFramePtr - origFramePtr) + frameSize;
  ++fUseCount;

  rtpSeqNo = fRTPSeqNo;
  rtpTimestamp = fRTPTimestamp;
  presentationTime = fPresentationTime;
  hasBeenSyncedUsingRTCP = fHasBeenSyncedUsingRTCP;
  rtpMarkerBit = fRTPMarkerBit;

  // Update "fPresentationTime" for the next enclosed frame (if any):
  fPresentationTime.tv_usec += frameDurationInMicroseconds;
  if (fPresentationTime.tv_usec >= 1000000) {
    fPresentationTime.tv_sec += fPresentationTime.tv_usec/1000000;
    fPresentationTime.tv_usec = fPresentationTime.tv_usec%1000000;
  }
}

BufferedPacketFactory::BufferedPacketFactory() {
}

BufferedPacketFactory::~BufferedPacketFactory() {
}

BufferedPacket* BufferedPacketFactory
::createNewPacket(MultiFramedRTPSource* /*ourSource*/) {
  return new BufferedPacket;
}


////////// ReorderingPacketBuffer implementation //////////

ReorderingPacketBuffer
::ReorderingPacketBuffer(BufferedPacketFactory* packetFactory)
  : fThresholdTime(100000) /* default reordering threshold: 100 ms */,
    fHaveSeenFirstPacket(False), fHeadPacket(NULL), fTailPacket(NULL), fSavedPacket(NULL), fSavedPacketFree(True) {
  fPacketFactory = (packetFactory == NULL)
    ? (new BufferedPacketFactory)
    : packetFactory;
}

ReorderingPacketBuffer::~ReorderingPacketBuffer() {
  reset();
  delete fPacketFactory;
}

void ReorderingPacketBuffer::reset() {
  if (fSavedPacketFree) delete fSavedPacket; // because fSavedPacket is not in the list
  delete fHeadPacket; // will also delete fSavedPacket if it's in the list
  resetHaveSeenFirstPacket();
  fHeadPacket = fTailPacket = fSavedPacket = NULL;
}

BufferedPacket* ReorderingPacketBuffer::getFreePacket(MultiFramedRTPSource* ourSource) {
  if (fSavedPacket == NULL) { // we're being called for the first time
    fSavedPacket = fPacketFactory->createNewPacket(ourSource);
    fSavedPacketFree = True;
  }

  if (fSavedPacketFree == True) {
    fSavedPacketFree = False;
    return fSavedPacket;
  } else {
    return fPacketFactory->createNewPacket(ourSource);
  }
}

Boolean ReorderingPacketBuffer::storePacket(BufferedPacket* bPacket) {
  unsigned short rtpSeqNo = bPacket->rtpSeqNo();

  if (!fHaveSeenFirstPacket) {
    fNextExpectedSeqNo = rtpSeqNo; // initialization
    bPacket->isFirstPacket() = True;
    fHaveSeenFirstPacket = True;
  }
  
  // 如果遇到迟来的包 将会丢弃(也就是当前的包延迟到了)
  // Ignore this packet if its sequence number is less than the one
  // that we're looking for (in this case, it's been excessively delayed).
  if (seqNumLT(rtpSeqNo, fNextExpectedSeqNo)) return False;

  if (fTailPacket == NULL) {
    // Common case: There are no packets in the queue; this will be the first one:
    // 通常情况 没有包在队列 这是第一个
    bPacket->nextPacket() = NULL;
    fHeadPacket = fTailPacket = bPacket;
    return True;
  }

  if (seqNumLT(fTailPacket->rtpSeqNo(), rtpSeqNo)) {
    // The next-most common case: There are packets already in the queue; this packet arrived in order => put it at the tail:
    // 一般情况下 按顺序接收到包 也就是后来收到的包 序号大于 先前收到的包 
    bPacket->nextPacket() = NULL;
    fTailPacket->nextPacket() = bPacket;
    fTailPacket = bPacket;
    return True;
  } 

  if (rtpSeqNo == fTailPacket->rtpSeqNo()) {
    // This is a duplicate packet - ignore it
    return False;
  }

  // Rare case: This packet is out-of-order.  Run through the list (from the head), to figure out where it belongs:
  // 罕见情况  收到的包是没有循序的 所以要遍历 找到合适地方插入
  BufferedPacket* beforePtr = NULL;
  BufferedPacket* afterPtr = fHeadPacket;
  while (afterPtr != NULL) {
    if (seqNumLT(rtpSeqNo, afterPtr->rtpSeqNo())) break; // it comes here
    if (rtpSeqNo == afterPtr->rtpSeqNo()) {
      // This is a duplicate packet - ignore it
      return False;
    }

    beforePtr = afterPtr;
    afterPtr = afterPtr->nextPacket();
  }

  // Link our new packet between "beforePtr" and "afterPtr":
  bPacket->nextPacket() = afterPtr;
  if (beforePtr == NULL) {
    fHeadPacket = bPacket;
  } else {
    beforePtr->nextPacket() = bPacket;
  }

  return True;
}

void ReorderingPacketBuffer::releaseUsedPacket(BufferedPacket* packet) {
  // ASSERT: packet == fHeadPacket
  // ASSERT: fNextExpectedSeqNo == packet->rtpSeqNo()
  ++fNextExpectedSeqNo; // because we're finished with this packet now

  fHeadPacket = fHeadPacket->nextPacket();
  if (!fHeadPacket) { 
    fTailPacket = NULL;
  }
  packet->nextPacket() = NULL;

  freePacket(packet);
}

BufferedPacket* ReorderingPacketBuffer
::getNextCompletedPacket(Boolean& packetLossPreceded) {
  if (fHeadPacket == NULL) return NULL;

  // Check whether the next packet we want is already at the head
  // of the queue:
  // ASSERT: fHeadPacket->rtpSeqNo() >= fNextExpectedSeqNo
  // 检查接收包中的序列号是否是所希望的序列号
  if (fHeadPacket->rtpSeqNo() == fNextExpectedSeqNo) {
    packetLossPreceded = fHeadPacket->isFirstPacket();
        // (The very first packet is treated as if there was packet loss beforehand.)
    return fHeadPacket;
  }

  // We're still waiting for our desired packet to arrive.  However, if
  // our time threshold has been exceeded, then forget it, and return
  // the head packet instead:
  // 等待所希望的包的到来，一段时间后丢弃
  Boolean timeThresholdHasBeenExceeded;
  if (fThresholdTime == 0) {
    timeThresholdHasBeenExceeded = True; // optimization
  } else {
    struct timeval timeNow;
    gettimeofday(&timeNow, NULL);
    unsigned uSecondsSinceReceived
      = (timeNow.tv_sec - fHeadPacket->timeReceived().tv_sec)*1000000
      + (timeNow.tv_usec - fHeadPacket->timeReceived().tv_usec);
    timeThresholdHasBeenExceeded = uSecondsSinceReceived > fThresholdTime;
  }
  if (timeThresholdHasBeenExceeded) {
    fNextExpectedSeqNo = fHeadPacket->rtpSeqNo();
        // we've given up on earlier packets now
    packetLossPreceded = True;
    return fHeadPacket;
  }

  // Otherwise, keep waiting for our desired packet to arrive:
  return NULL;
}
