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
// A 'ServerMediaSubsession' object that creates new, unicast, "RTPSink"s
// on demand.
// Implementation

#include "OnDemandServerMediaSubsession.hh"
#include <GroupsockHelper.hh>

OnDemandServerMediaSubsession
::OnDemandServerMediaSubsession(UsageEnvironment& env,
				Boolean reuseFirstSource,
				portNumBits initialPortNum, // 派生类可以指定 最初的rtp端口 默认6970
				Boolean multiplexRTCPWithRTP)// rtp和rtcp复用一个端口 				默认false
  : ServerMediaSubsession(env),
    fSDPLines(NULL), fReuseFirstSource(reuseFirstSource),
    fMultiplexRTCPWithRTP(multiplexRTCPWithRTP), fLastStreamToken(NULL),
    fAppHandlerTask(NULL), fAppHandlerClientData(NULL) {
  fDestinationsHashTable = HashTable::create(ONE_WORD_HASH_KEYS);
  if (fMultiplexRTCPWithRTP) {
    fInitialPortNum = initialPortNum;
  } else {
    // Make sure RTP ports are even-numbered:
    fInitialPortNum = (initialPortNum+1)&~1; // 初始化端口 必须是偶数 
  }
  gethostname(fCNAME, sizeof fCNAME);
  fCNAME[sizeof fCNAME-1] = '\0'; // just in case
}

OnDemandServerMediaSubsession::~OnDemandServerMediaSubsession() {
  delete[] fSDPLines;

  // Clean out the destinations hash table:
  while (1) {
    Destinations* destinations
      = (Destinations*)(fDestinationsHashTable->RemoveNext());
    if (destinations == NULL) break;
    delete destinations;
  }
  delete fDestinationsHashTable;
}

char const*
OnDemandServerMediaSubsession::sdpLines() {
  if (fSDPLines == NULL) { // 虽然ServerMediaSubsession实例可能已经创建 但是fSDPLines可能在客户端DESCRITE的时候才会创建一次 以后就不用了
    // We need to construct a set of SDP lines that describe this
    // subsession (as a unicast stream).  To do so, we first create
    // dummy (unused) source and "RTPSink" objects, < 需要首先创建无用的FramedSource和RTPSink实例
    // whose parameters we use for the SDP lines:
    unsigned estBitrate;

    // 虚函数!
    FramedSource* inputSource = createNewStreamSource(0, estBitrate); 
    if (inputSource == NULL) return NULL; // file not found

    struct in_addr dummyAddr;
    dummyAddr.s_addr = 0;
    Groupsock* dummyGroupsock = createGroupsock(dummyAddr, 0);
    unsigned char rtpPayloadType = 96 + trackNumber()-1; // if dynamic

    // 虚函数
    RTPSink* dummyRTPSink = createNewRTPSink(dummyGroupsock, rtpPayloadType, inputSource);
    if (dummyRTPSink != NULL && dummyRTPSink->estimatedBitrate() > 0) estBitrate = dummyRTPSink->estimatedBitrate();

    setSDPLinesFromRTPSink(dummyRTPSink, inputSource, estBitrate); // SDP信息 会回调RTPSink和FramedSource的多个函数获取SDP信息
    
    Medium::close(dummyRTPSink);	// 删除临时创建的 FramedSource
    delete dummyGroupsock; 			
    closeStreamSource(inputSource);	// 删除临时创建的 RTPSink
  }

  return fSDPLines;			// 后面 getStreamParameters 的时候 再会创建FramedSource和RTPSink实例 
}

void OnDemandServerMediaSubsession	// handleCmd_SETUP SETUP阶段 回调 
::getStreamParameters(unsigned clientSessionId,
		      netAddressBits clientAddress,
		      Port const& clientRTPPort,
		      Port const& clientRTCPPort,
		      int tcpSocketNum,
		      unsigned char rtpChannelId,
		      unsigned char rtcpChannelId,
		      netAddressBits& destinationAddress,
		      u_int8_t& /*destinationTTL*/,
		      Boolean& isMulticast,
		      Port& serverRTPPort,
		      Port& serverRTCPPort,
		      void*& streamToken) {
  if (destinationAddress == 0) destinationAddress = clientAddress;
  struct in_addr destinationAddr; destinationAddr.s_addr = destinationAddress;
  isMulticast = False;

  if (fLastStreamToken != NULL && fReuseFirstSource) {
    // Special case: Rather than creating a new 'StreamState',
    // we reuse the one that we've already created:
    serverRTPPort = ((StreamState*)fLastStreamToken)->serverRTPPort();
    serverRTCPPort = ((StreamState*)fLastStreamToken)->serverRTCPPort();
    ++((StreamState*)fLastStreamToken)->referenceCount();
    streamToken = fLastStreamToken; 
	// fReuseFirstSource = true 
	// 如果有新的客户端 发送另外一个连接/会话 对同一个streamName进行播放 (rtsp://192.168.1.123:8086/mystream/3.gp )
	// SETUP rtsp://192.168.1.123:8086/mystream/3.gp/track0
	// SETUP rtsp://192.168.1.123:8086/mystream/3.gp/track1
	// PLAY rtsp://192.168.1.123:8086/mystream/3.gp 
	// streamName = mystream/3.gp --> 根据这个streamName 可以在 RtspServer::HashTable* fServerMediaSessions 查找到之前创建的 ServerMediaSession(假设之前已有客户端链接并对同样的streamName构建的ServerMediaSession)
	// 然后 ServerMediaSession 中的SubSession之前已经 回调 createNewStreamSource和createNewRTPSink
	// 这里重复使用 这个 Source和Sink 也就是回复客户端 同个UDP和RTCP端口
  } else {
    // Normal case: Create a new media source:
    unsigned streamBitrate;
    FramedSource* mediaSource	// 虚函数 创建FramedSource实例
      = createNewStreamSource(clientSessionId, streamBitrate);// streamBitrate 用来计算发送socket buffer的大小

    // Create 'groupsock' and 'sink' objects for the destination,
    // using previously unused server port numbers:
    RTPSink* rtpSink = NULL;
    BasicUDPSink* udpSink = NULL;
    Groupsock* rtpGroupsock = NULL;
    Groupsock* rtcpGroupsock = NULL;

	// 参数tcpSocketNum   是  in (-1 means use UDP, not TCP)
    if (clientRTPPort.num() != 0 || tcpSocketNum >= 0) { // Normal case: Create destinations
		portNumBits serverPortNum;
		// RtspServer::parseTransportHeader 
		// tcpSocketNum >= 0  ---->  RTP over TCP
		// clientRTPPort.num() != 0 --> UDP or RTP over UDP 
		// clientRTCPPort.num() == 0 --> UDP (RTP over TCP是 1， RTP over UDP是具体的端口号)	
      	if (clientRTCPPort.num() == 0) { 	 
		// ---- 使用 raw UDP (不是RTP)   	
		// We're streaming raw UDP (not RTP). Create a single groupsock:
			NoReuse dummy(envir()); // ensures that we skip over ports that are already in use
			for (serverPortNum = fInitialPortNum; ; ++serverPortNum) {
				  struct in_addr dummyAddr; dummyAddr.s_addr = 0;

				  /*
				  	portNumBits initialPortNum = 6970, 
				  	Boolean multiplexRTCPWithRTP = False);
				  */
				  serverRTPPort = serverPortNum; 
					// Port = portNumBits
				  	// 隐式构造 Port(portNumBits)
				  	// 默认赋值函数 Port = Port(portNumBits)
				  rtpGroupsock = createGroupsock(dummyAddr, serverRTPPort);
				  if (rtpGroupsock->socketNum() >= 0) break; // success
			}// 创建UDPSink 而不是RTPSink
			udpSink = BasicUDPSink::createNew(envir(), rtpGroupsock);
      } else { 								
	  // ---- 使用 RTP over UDP or TCP
      // Normal case: We're streaming RTP (over UDP or TCP).  Create a pair of
      // groupsocks (RTP and RTCP), with adjacent port numbers (RTP port number even).
      // (If we're multiplexing RTCP and RTP over the same port number, it can be odd or even.)
			NoReuse dummy(envir()); // ensures that we skip over ports that are already in use
			for (portNumBits serverPortNum = fInitialPortNum; ; ++serverPortNum) {
				// 每次从 初始端口 开始检查 
		  		struct in_addr dummyAddr; dummyAddr.s_addr = 0;

				serverRTPPort = serverPortNum;
				rtpGroupsock = createGroupsock(dummyAddr, serverRTPPort);
				if (rtpGroupsock->socketNum() < 0) { // 判断端口是否创建成功
					delete rtpGroupsock;
					continue; // try again
				}

				if (fMultiplexRTCPWithRTP) {
					// Use the RTP 'groupsock' object for RTCP as well:
					serverRTCPPort = serverRTPPort;
					rtcpGroupsock = rtpGroupsock;
				} else {
					// Create a separate 'groupsock' object (with the next (odd) port number) for RTCP:
					serverRTCPPort = ++serverPortNum;// rtp和rtcp的port必须是连续的 否则两者都要重建
					rtcpGroupsock = createGroupsock(dummyAddr, serverRTCPPort);
					if (rtcpGroupsock->socketNum() < 0) {
						delete rtpGroupsock;
						delete rtcpGroupsock;
						continue; // try again
					}
		  		}
		  		break; // success
			}// 这里已经创建好 rtpGroupsock 和 rtcpGroupsock

			unsigned char rtpPayloadType = 96 + trackNumber()-1; // if dynamic
			//	虚函数 createNewRTPSink  返回 RTPSink实例 
			rtpSink = createNewRTPSink(rtpGroupsock, rtpPayloadType, mediaSource);
			if (rtpSink != NULL && rtpSink->estimatedBitrate() > 0) streamBitrate = rtpSink->estimatedBitrate();
	  }

	  // Turn off the destinations for each groupsock.  They'll get set later
	  // (unless TCP is used instead):
	  if (rtpGroupsock != NULL) rtpGroupsock->removeAllDestinations();
	  if (rtcpGroupsock != NULL) rtcpGroupsock->removeAllDestinations();

	  if (rtpGroupsock != NULL) {
	  	 // 根据createNewStreamSource 数据源返回的bitrate来估计 RTP socket发送缓存区大小
	      // Try to use a big send buffer for RTP -  at least 0.1 second of
	      // specified bandwidth and at least 50 KB
	      unsigned rtpBufSize = streamBitrate * 25 / 2; // 1 kbps * 0.1 s = 12.5 bytes
	      // 根据 createNewStreamSource 提供的比特率(bit)          计算  0.1s需要的buffer大小(字节)
	      // bitrate * 1000 *  0.1s / 8bit 
	      if (rtpBufSize < 50 * 1024) rtpBufSize = 50 * 1024;

		  // 控制服务器 RTP socket的buffer大小
	      increaseSendBufferTo(envir(), rtpGroupsock->socketNum(), rtpBufSize);
		}
   }

    // Set up the state of the stream.  The stream will get started later:
    streamToken = fLastStreamToken
      = new StreamState(*this, serverRTPPort, serverRTCPPort, rtpSink, udpSink,
			streamBitrate, mediaSource,
			rtpGroupsock, rtcpGroupsock);
    
    /* 
   		当streamName一样的时候 就会用原来的ServerMediaSession 
		ServerMediaSession和ServerMediaSubSession都可能被多个Client使用

		但是会根据 创建ServerMediaSubSession时，是否 fReuseFirstSource，
 		fReuseFirstSource = true ，对多个Client使用同一个RtpSInk FrameSource(同一个rtp rtcp端口)
 		fReuseFirstSource = false，虽然是同一个 ServerMediaSession和 ServerMediaSubSession 
 								但是每个Client使用不同的RtpSInk和FrameSource(不同的rtp和rtcp端口) ，
 								并通过StreamState( 保存对应的RtpSink FrameSouce 
 													rtp/rtcp端口 interleave/channel 
 													和共同的ServerMediaSubsession对象) 返回 

		StreamState 就相当于 客户端ClientSession对同个流(StreamName StreamSession)的单独状态和控制
		
		因为存在 ServerMediaSession和ServerMediaSubSession 可以共享 
		所以也不用每次DESCIRBE命令都创建新的 ServerMediaSession 再回复SDP-line
	
    	StreamState类 
    		服务器端用来保存对某个ServerMediaSubsession的流化的状态 包括:
    		serverRTPPort		Port	
    		serverRTCPPort		Port 
    		rtpSink 			RTPSink
    		mediaSource 		FramedSource
    		this				ServerMediaSubsession 

    	在创建一个ServerMediaSubsession对象时, 会传入reuseFirstSource这个参数
    		参考 testOnDemandRTSPServer.cpp的main函数

    		
    		reuseFirstSource为true  
    			请求该ServerMediaSubsession的所有客户端都使用同一个StreamState对象
    			即服务器端使用同一个RTP端口、RTCP端口、RTPSink、FramedSource来为请求该ServerMediaSubsession的多个客户端服务
    			（一对多，节省服务器端资源）

			reuseFirstSource为false	
				服务器端为每个对ServerMediaSubsession的请求创建一个StreamState对象
				（多对多，需要占用服务器端较多资源）

		fStreamStates 是 streamState数组     RTSPClientSession 的属性

    */ 
    
  	}

	// Record these destinations as being for this client session id:
	Destinations* destinations;
	if (tcpSocketNum < 0) { // UDP
	    destinations = new Destinations(destinationAddr, clientRTPPort, clientRTCPPort);
	} else { // TCP tcpSocketNum=rtsp端口  rtpChannelId=0  rtcpChannelId=1 
		destinations = new Destinations(tcpSocketNum, rtpChannelId, rtcpChannelId);
	}
	// 保存RTP/RTCP包发送的目的地 
	// UDP=destinationAddr  clientRTPPort, clientRTCPPort 	ip地址				rtp/rtcp端口
	// TCP=tcpSocketNum  rtpChannelId, rtcpChannelId		RtspTCPsocket套接字 rtp/rtcp通道
	//
	// 在 StreamState::startPlaying 会设置好发送的channelID/port
	//
  	fDestinationsHashTable->Add((char const*)clientSessionId, destinations);
}

void OnDemandServerMediaSubsession::startStream(unsigned clientSessionId,
						void* streamToken,
						TaskFunc* rtcpRRHandler, 
						/*
						@ RTSPServer.cpp 
						
						RTSPServer::RTSPClientSession::handleCmd_PLAY 处理客户端Play命令时候 

						fStreamStates[i].subsession->startStream
						*/
						void* rtcpRRHandlerClientData,
						unsigned short& rtpSeqNum,
						unsigned& rtpTimestamp,
						ServerRequestAlternativeByteHandler* serverRequestAlternativeByteHandler,
						void* serverRequestAlternativeByteHandlerClientData) {
  StreamState* streamState = (StreamState*)streamToken;
  Destinations* destinations
    = (Destinations*)(fDestinationsHashTable->Lookup((char const*)clientSessionId));
  if (streamState != NULL) {
    streamState->startPlaying(destinations, clientSessionId,
			      rtcpRRHandler, rtcpRRHandlerClientData,
			      serverRequestAlternativeByteHandler, serverRequestAlternativeByteHandlerClientData);
    RTPSink* rtpSink = streamState->rtpSink(); // alias
    if (rtpSink != NULL) {
      rtpSeqNum = rtpSink->currentSeqNo();
      rtpTimestamp = rtpSink->presetNextTimestamp();
    }
  }
}

void OnDemandServerMediaSubsession::pauseStream(unsigned /*clientSessionId*/,
						void* streamToken) {
  // Pausing isn't allowed if multiple clients are receiving data from
  // the same source:
  // 在多客户端重复使用同一个Source的情况下 Pause/Seek命令不能使用
  if (fReuseFirstSource) return;

  StreamState* streamState = (StreamState*)streamToken;
  if (streamState != NULL) streamState->pause();
}

void OnDemandServerMediaSubsession::seekStream(unsigned /*clientSessionId*/,
					       void* streamToken, double& seekNPT, double streamDuration, u_int64_t& numBytes) {
  numBytes = 0; // by default: unknown

  // Seeking isn't allowed if multiple clients are receiving data from the same source:
  if (fReuseFirstSource) return;

  StreamState* streamState = (StreamState*)streamToken;
  if (streamState != NULL && streamState->mediaSource() != NULL) {
    seekStreamSource(streamState->mediaSource(), seekNPT, streamDuration, numBytes);

    streamState->startNPT() = (float)seekNPT;
    RTPSink* rtpSink = streamState->rtpSink(); // alias
    if (rtpSink != NULL) rtpSink->resetPresentationTimes();
  }
}

void OnDemandServerMediaSubsession::seekStream(unsigned /*clientSessionId*/,
					       void* streamToken, char*& absStart, char*& absEnd) {
  // Seeking isn't allowed if multiple clients are receiving data from the same source:
  if (fReuseFirstSource) return;

  StreamState* streamState = (StreamState*)streamToken;
  if (streamState != NULL && streamState->mediaSource() != NULL) {
    seekStreamSource(streamState->mediaSource(), absStart, absEnd);
  }
}

void OnDemandServerMediaSubsession::nullSeekStream(unsigned /*clientSessionId*/, void* streamToken,
						   double streamEndTime, u_int64_t& numBytes) {
  numBytes = 0; // by default: unknown

  StreamState* streamState = (StreamState*)streamToken;
  if (streamState != NULL && streamState->mediaSource() != NULL) {
    // Because we're not seeking here, get the current NPT, and remember it as the new 'start' NPT:
    streamState->startNPT() = getCurrentNPT(streamToken);

    double duration = streamEndTime - streamState->startNPT();
    if (duration < 0.0) duration = 0.0;
    setStreamSourceDuration(streamState->mediaSource(), duration, numBytes);

    RTPSink* rtpSink = streamState->rtpSink(); // alias
    if (rtpSink != NULL) rtpSink->resetPresentationTimes();
  }
}

void OnDemandServerMediaSubsession::setStreamScale(unsigned /*clientSessionId*/,
						   void* streamToken, float scale) {
  // Changing the scale factor isn't allowed if multiple clients are receiving data
  // from the same source:
  if (fReuseFirstSource) return;

  StreamState* streamState = (StreamState*)streamToken;
  if (streamState != NULL && streamState->mediaSource() != NULL) {
    setStreamSourceScale(streamState->mediaSource(), scale);
  }
}

float OnDemandServerMediaSubsession::getCurrentNPT(void* streamToken) {
  do {
    if (streamToken == NULL) break;

    StreamState* streamState = (StreamState*)streamToken;
    RTPSink* rtpSink = streamState->rtpSink();
    if (rtpSink == NULL) break;

    return streamState->startNPT()
      + (rtpSink->mostRecentPresentationTime().tv_sec - rtpSink->initialPresentationTime().tv_sec)
      + (rtpSink->mostRecentPresentationTime().tv_usec - rtpSink->initialPresentationTime().tv_usec)/1000000.0f;
  } while (0);

  return 0.0;
}

FramedSource* OnDemandServerMediaSubsession::getStreamSource(void* streamToken) {
  if (streamToken == NULL) return NULL;

  StreamState* streamState = (StreamState*)streamToken;
  return streamState->mediaSource();
}

void OnDemandServerMediaSubsession
::getRTPSinkandRTCP(void* streamToken,
		    RTPSink const*& rtpSink, RTCPInstance const*& rtcp) {
  if (streamToken == NULL) {
    rtpSink = NULL;
    rtcp = NULL;
    return;
  }

  StreamState* streamState = (StreamState*)streamToken;
  rtpSink = streamState->rtpSink();
  rtcp = streamState->rtcpInstance();
}

void OnDemandServerMediaSubsession::deleteStream(unsigned clientSessionId,
						 void*& streamToken) {


  fprintf(stderr , "OnDemandServerMediaSubsession::deleteStream session id = %x" , clientSessionId);						
  StreamState* streamState = (StreamState*)streamToken;

  // Look up (and remove) the destinations for this client session:
  Destinations* destinations
    = (Destinations*)(fDestinationsHashTable->Lookup((char const*)clientSessionId));
  if (destinations != NULL) {
    fDestinationsHashTable->Remove((char const*)clientSessionId);

    // Stop streaming to these destinations:
    if (streamState != NULL) streamState->endPlaying(destinations, clientSessionId);
  }

  // Delete the "StreamState" structure if it's no longer being used:
  if (streamState != NULL) {
    if (streamState->referenceCount() > 0) --streamState->referenceCount();
    if (streamState->referenceCount() == 0) {
      delete streamState;
      streamToken = NULL;
    }
  }

  // Finally, delete the destinations themselves:
  delete destinations;
}

char const* OnDemandServerMediaSubsession
::getAuxSDPLine(RTPSink* rtpSink, FramedSource* /*inputSource*/) {
  // Default implementation:
  return rtpSink == NULL ? NULL : rtpSink->auxSDPLine();
}

void OnDemandServerMediaSubsession::seekStreamSource(FramedSource* /*inputSource*/,
						     double& /*seekNPT*/, double /*streamDuration*/, u_int64_t& numBytes) {
  // Default implementation: Do nothing
  numBytes = 0;
}

void OnDemandServerMediaSubsession::seekStreamSource(FramedSource* /*inputSource*/,
						     char*& absStart, char*& absEnd) {
  // Default implementation: do nothing (but delete[] and assign "absStart" and "absEnd" to NULL, to show that we don't handle this)
  delete[] absStart; absStart = NULL;
  delete[] absEnd; absEnd = NULL;
}

void OnDemandServerMediaSubsession
::setStreamSourceScale(FramedSource* /*inputSource*/, float /*scale*/) {
  // Default implementation: Do nothing
}

void OnDemandServerMediaSubsession
::setStreamSourceDuration(FramedSource* /*inputSource*/, double /*streamDuration*/, u_int64_t& numBytes) {
  // Default implementation: Do nothing
  numBytes = 0;
}

void OnDemandServerMediaSubsession::closeStreamSource(FramedSource *inputSource) {
  Medium::close(inputSource);
}

Groupsock* OnDemandServerMediaSubsession
::createGroupsock(struct in_addr const& addr, Port port) {
  // Default implementation; may be redefined by subclasses:
  return new Groupsock(envir(), addr, port, 255);
}

RTCPInstance* OnDemandServerMediaSubsession
::createRTCP(Groupsock* RTCPgs, unsigned totSessionBW, /* in kbps */
	     unsigned char const* cname, RTPSink* sink) {
  // Default implementation; may be redefined by subclasses:
  return RTCPInstance::createNew(envir(), RTCPgs, totSessionBW, cname, sink, NULL/*we're a server*/);
}

void OnDemandServerMediaSubsession
::setRTCPAppPacketHandler(RTCPAppHandlerFunc* handler, void* clientData) {
  fAppHandlerTask = handler;
  fAppHandlerClientData = clientData;
}

void OnDemandServerMediaSubsession
::sendRTCPAppPacket(u_int8_t subtype, char const* name,
		    u_int8_t* appDependentData, unsigned appDependentDataSize) {
  StreamState* streamState = (StreamState*)fLastStreamToken;
  if (streamState != NULL) {
    streamState->sendRTCPAppPacket(subtype, name, appDependentData, appDependentDataSize);
  }
}

void OnDemandServerMediaSubsession
::setSDPLinesFromRTPSink(RTPSink* rtpSink, FramedSource* inputSource, unsigned estBitrate) {
  if (rtpSink == NULL) return;

  char const* mediaType = rtpSink->sdpMediaType();
  unsigned char rtpPayloadType = rtpSink->rtpPayloadType();
  AddressString ipAddressStr(fServerAddressForSDP);
  char* rtpmapLine = rtpSink->rtpmapLine();
  char const* rtcpmuxLine = fMultiplexRTCPWithRTP ? "a=rtcp-mux\r\n" : "";
  char const* rangeLine = rangeSDPLine();
  char const* auxSDPLine = getAuxSDPLine(rtpSink, inputSource);
  if (auxSDPLine == NULL) auxSDPLine = "";

  char const* const sdpFmt =
    "m=%s %u RTP/AVP %d\r\n"
    "c=IN IP4 %s\r\n"
    "b=AS:%u\r\n"
    "%s"
    "%s"
    "%s"
    "%s"
    "a=control:%s\r\n";
  unsigned sdpFmtSize = strlen(sdpFmt)
    + strlen(mediaType) + 5 /* max short len */ + 3 /* max char len */
    + strlen(ipAddressStr.val())
    + 20 /* max int len */
    + strlen(rtpmapLine)
    + strlen(rtcpmuxLine)
    + strlen(rangeLine)
    + strlen(auxSDPLine)
    + strlen(trackId());
  char* sdpLines = new char[sdpFmtSize];
  sprintf(sdpLines, sdpFmt,
	  mediaType, // m= <media>
	  fPortNumForSDP, // m= <port>
	  rtpPayloadType, // m= <fmt list>
	  ipAddressStr.val(), // c= address
	  estBitrate, // b=AS:<bandwidth>
	  rtpmapLine, // a=rtpmap:... (if present)
	  rtcpmuxLine, // a=rtcp-mux:... (if present)
	  rangeLine, // a=range:... (if present)
	  auxSDPLine, // optional extra SDP line
	  trackId()); // a=control:<track-id>
  delete[] (char*)rangeLine; delete[] rtpmapLine;

  fSDPLines = strDup(sdpLines);
  delete[] sdpLines;
}


////////// StreamState implementation //////////

static void afterPlayingStreamState(void* clientData) {
  StreamState* streamState = (StreamState*)clientData;
  if (streamState->streamDuration() == 0.0) {
    // When the input stream ends, tear it down.  This will cause a RTCP "BYE"
    // to be sent to each client, teling it that the stream has ended.
    // (Because the stream didn't have a known duration, there was no other
    //  way for clients to know when the stream ended.)
    streamState->reclaim();
  }
  // Otherwise, keep the stream alive, in case a client wants to
  // subsequently re-play the stream starting from somewhere other than the end.
  // (This can be done only on streams that have a known duration.)
}

StreamState::StreamState(OnDemandServerMediaSubsession& master,// 每个ServerMediaSubsession对应一个StreamState
                         Port const& serverRTPPort, Port const& serverRTCPPort,
			 RTPSink* rtpSink, BasicUDPSink* udpSink,
			 unsigned totalBW, FramedSource* mediaSource,
			 Groupsock* rtpGS, Groupsock* rtcpGS)
  : fMaster(master), fAreCurrentlyPlaying(False), fReferenceCount(1),
    fServerRTPPort(serverRTPPort), fServerRTCPPort(serverRTCPPort),
    fRTPSink(rtpSink), fUDPSink(udpSink), fStreamDuration(master.duration()),
    fTotalBW(totalBW), fRTCPInstance(NULL) /* created later */,
    fMediaSource(mediaSource), fStartNPT(0.0), fRTPgs(rtpGS), fRTCPgs(rtcpGS) {
}

StreamState::~StreamState() {
  reclaim();
}

void StreamState
::startPlaying(Destinations* dests, unsigned clientSessionId,
	       TaskFunc* rtcpRRHandler, void* rtcpRRHandlerClientData,
	       ServerRequestAlternativeByteHandler* serverRequestAlternativeByteHandler,
	       void* serverRequestAlternativeByteHandlerClientData) {
  if (dests == NULL) return;

  if (fRTCPInstance == NULL && fRTPSink != NULL) {
  	// 一个RTP对应一个RTCP实例 
    // Create (and start) a 'RTCP instance' for this RTP sink:
    fRTCPInstance = fMaster.createRTCP(fRTCPgs, fTotalBW, (unsigned char*)fMaster.fCNAME, fRTPSink);
        // Note: This starts RTCP running automatically
    fRTCPInstance->setAppHandler(fMaster.fAppHandlerTask, fMaster.fAppHandlerClientData);
  }

  if (dests->isTCP) {// 使用TCP传输RTP包和RTCP包
  
    // Change RTP and RTCP to use the TCP socket instead of UDP:
    if (fRTPSink != NULL) {

	// 把socket给到RTPSink  这样通过RTSP端口发送RTP包 但是还是可以通过RTSP端口接收RTP包
      fRTPSink->addStreamSocket(dests->tcpSocketNum, dests->rtpChannelId);
      RTPInterface
	::setServerRequestAlternativeByteHandler(fRTPSink->envir(), dests->tcpSocketNum,
						 serverRequestAlternativeByteHandler, serverRequestAlternativeByteHandlerClientData);
        // So that we continue to handle RTSP commands from the client
    }
    if (fRTCPInstance != NULL) { //TCP 如果使用RTCP的话 这里添加 rtcpRRHandler(maybe GenericMediaServer::ClientSession::noteClientLiveness )	
      fRTCPInstance->addStreamSocket(dests->tcpSocketNum, dests->rtcpChannelId);
      fRTCPInstance->setSpecificRRHandler(dests->tcpSocketNum, dests->rtcpChannelId,
					  rtcpRRHandler, rtcpRRHandlerClientData);
    }
  } else {
    // Tell the RTP and RTCP 'groupsocks' about this destination
    // (in case they don't already have it):
    if (fRTPgs != NULL) fRTPgs->addDestination(dests->addr, dests->rtpPort, clientSessionId);
    if (fRTCPgs != NULL && !(fRTCPgs == fRTPgs && dests->rtcpPort.num() == dests->rtpPort.num())) {
      fRTCPgs->addDestination(dests->addr, dests->rtcpPort, clientSessionId);
    }
    if (fRTCPInstance != NULL) {//UDP  
      fRTCPInstance->setSpecificRRHandler(dests->addr.s_addr, dests->rtcpPort,
					  rtcpRRHandler, rtcpRRHandlerClientData);
    }
  }

  if (fRTCPInstance != NULL) {
    // Hack: Send an initial RTCP "SR" packet, before the initial RTP packet, so that receivers will (likely) be able to
    // get RTCP-synchronized presentation times immediately:
    fRTCPInstance->sendReport();
  }

  if (!fAreCurrentlyPlaying && fMediaSource != NULL) {
    if (fRTPSink != NULL) {// 这里开始MediaSink::startPlaying 开始播放
      fRTPSink->startPlaying(*fMediaSource, afterPlayingStreamState, this);
      fAreCurrentlyPlaying = True;
    } else if (fUDPSink != NULL) {
      fUDPSink->startPlaying(*fMediaSource, afterPlayingStreamState, this);
      fAreCurrentlyPlaying = True;
    }
  }
}

void StreamState::pause() {
  if (fRTPSink != NULL) fRTPSink->stopPlaying();
  if (fUDPSink != NULL) fUDPSink->stopPlaying();
  fAreCurrentlyPlaying = False;
}

void StreamState::endPlaying(Destinations* dests, unsigned clientSessionId) {
#if 0
  // The following code is temporarily disabled, because it erroneously sends RTCP "BYE"s to all clients if multiple
  // clients are streaming from the same data source (i.e., if "reuseFirstSource" is True), and we don't want that to happen
  // if we're being called as a result of a single one of these clients having sent a "TEARDOWN" (rather than the whole stream
  // having been closed, for all clients).
  // This will be fixed for real later.
  if (fRTCPInstance != NULL) {
    // Hack: Explicitly send a RTCP "BYE" packet now, because the code below will prevent that from happening later,
    // when "fRTCPInstance" gets deleted:
    fRTCPInstance->sendBYE();
  }
#endif

  if (dests->isTCP) {
    if (fRTPSink != NULL) {
      fRTPSink->removeStreamSocket(dests->tcpSocketNum, dests->rtpChannelId);
    }
    if (fRTCPInstance != NULL) {
      fRTCPInstance->removeStreamSocket(dests->tcpSocketNum, dests->rtcpChannelId);
      fRTCPInstance->unsetSpecificRRHandler(dests->tcpSocketNum, dests->rtcpChannelId);
    }
  } else {
    // Tell the RTP and RTCP 'groupsocks' to stop using these destinations:
    if (fRTPgs != NULL) fRTPgs->removeDestination(clientSessionId);
    if (fRTCPgs != NULL && fRTCPgs != fRTPgs) fRTCPgs->removeDestination(clientSessionId);
    if (fRTCPInstance != NULL) {
      fRTCPInstance->unsetSpecificRRHandler(dests->addr.s_addr, dests->rtcpPort);
    }
  }
}

void StreamState::sendRTCPAppPacket(u_int8_t subtype, char const* name,
				    u_int8_t* appDependentData, unsigned appDependentDataSize) {
  if (fRTCPInstance != NULL) {
    fRTCPInstance->sendAppPacket(subtype, name, appDependentData, appDependentDataSize);
  }
}

void StreamState::reclaim() {
  // Delete allocated media objects
  Medium::close(fRTCPInstance) /* will send a RTCP BYE */; fRTCPInstance = NULL;
  Medium::close(fRTPSink); fRTPSink = NULL;
  Medium::close(fUDPSink); fUDPSink = NULL;

  fMaster.closeStreamSource(fMediaSource); fMediaSource = NULL;
  if (fMaster.fLastStreamToken == this) fMaster.fLastStreamToken = NULL;

  delete fRTPgs;
  if (fRTCPgs != fRTPgs) delete fRTCPgs;
  fRTPgs = NULL; fRTCPgs = NULL;
}
