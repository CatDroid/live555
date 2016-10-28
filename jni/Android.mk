LOCAL_PATH := $(call my-dir)




include $(CLEAR_VARS)  
LOCAL_MODULE := live555  

LOCAL_SRC_FILES := \
	live/groupsock/GroupEId.cpp \
	live/groupsock/IOHandlers.cpp \
	live/groupsock/NetInterface.cpp \
	live/groupsock/NetAddress.cpp \
	live/groupsock/GroupsockHelper.cpp \
	live/groupsock/Groupsock.cpp \
	live/groupsock/inet.c \
	live/BasicUsageEnvironment/DelayQueue.cpp \
	live/BasicUsageEnvironment/BasicHashTable.cpp \
	live/BasicUsageEnvironment/BasicUsageEnvironment.cpp \
	live/BasicUsageEnvironment/BasicUsageEnvironment0.cpp \
	live/BasicUsageEnvironment/BasicTaskScheduler.cpp \
	live/BasicUsageEnvironment/BasicTaskScheduler0.cpp 	\
	live/UsageEnvironment/HashTable.cpp \
	live/UsageEnvironment/strDup.cpp \
	live/UsageEnvironment/UsageEnvironment.cpp \
	live/liveMedia/AC3AudioFileServerMediaSubsession.cpp                   \
	live/liveMedia/AC3AudioRTPSink.cpp                                     \
	live/liveMedia/AC3AudioRTPSource.cpp                                   \
	live/liveMedia/AC3AudioStreamFramer.cpp                                \
	live/liveMedia/ADTSAudioFileServerMediaSubsession.cpp                  \
	live/liveMedia/ADTSAudioFileSource.cpp                                 \
	live/liveMedia/AMRAudioFileServerMediaSubsession.cpp                   \
	live/liveMedia/AMRAudioFileSink.cpp                                    \
	live/liveMedia/AMRAudioFileSource.cpp                                  \
	live/liveMedia/AMRAudioRTPSink.cpp                                     \
	live/liveMedia/AMRAudioRTPSource.cpp                                   \
	live/liveMedia/AMRAudioSource.cpp                                      \
	live/liveMedia/AudioInputDevice.cpp                                    \
	live/liveMedia/AudioRTPSink.cpp                                        \
	live/liveMedia/AVIFileSink.cpp                                         \
	live/liveMedia/Base64.cpp                                              \
	live/liveMedia/BasicUDPSink.cpp                                        \
	live/liveMedia/BasicUDPSource.cpp                                      \
	live/liveMedia/BitVector.cpp                                           \
	live/liveMedia/ByteStreamFileSource.cpp                                \
	live/liveMedia/ByteStreamMemoryBufferSource.cpp                        \
	live/liveMedia/ByteStreamMultiFileSource.cpp                           \
	live/liveMedia/DeviceSource.cpp                                        \
	live/liveMedia/DigestAuthentication.cpp                                \
	live/liveMedia/DVVideoFileServerMediaSubsession.cpp                    \
	live/liveMedia/DVVideoRTPSink.cpp                                      \
	live/liveMedia/DVVideoRTPSource.cpp                                    \
	live/liveMedia/DVVideoStreamFramer.cpp                                 \
	live/liveMedia/EBMLNumber.cpp                                          \
	live/liveMedia/FileServerMediaSubsession.cpp                           \
	live/liveMedia/FileSink.cpp                                            \
	live/liveMedia/FramedFileSource.cpp                                    \
	live/liveMedia/FramedFilter.cpp                                        \
	live/liveMedia/FramedSource.cpp                                        \
	live/liveMedia/GenericMediaServer.cpp                                  \
	live/liveMedia/GSMAudioRTPSink.cpp                                     \
	live/liveMedia/H261VideoRTPSource.cpp                                  \
	live/liveMedia/H263plusVideoFileServerMediaSubsession.cpp              \
	live/liveMedia/H263plusVideoRTPSink.cpp                                \
	live/liveMedia/H263plusVideoRTPSource.cpp                              \
	live/liveMedia/H263plusVideoStreamFramer.cpp                           \
	live/liveMedia/H263plusVideoStreamParser.cpp                           \
	live/liveMedia/H264or5VideoFileSink.cpp                                \
	live/liveMedia/H264or5VideoRTPSink.cpp                                 \
	live/liveMedia/H264or5VideoStreamDiscreteFramer.cpp                    \
	live/liveMedia/H264or5VideoStreamFramer.cpp                            \
	live/liveMedia/H264VideoFileServerMediaSubsession.cpp                  \
	live/liveMedia/H264VideoFileSink.cpp                                   \
	live/liveMedia/H264VideoRTPSink.cpp                                    \
	live/liveMedia/H264VideoRTPSource.cpp                                  \
	live/liveMedia/H264VideoStreamDiscreteFramer.cpp                       \
	live/liveMedia/H264VideoStreamFramer.cpp                               \
	live/liveMedia/H265VideoFileServerMediaSubsession.cpp                  \
	live/liveMedia/H265VideoFileSink.cpp                                   \
	live/liveMedia/H265VideoRTPSink.cpp                                    \
	live/liveMedia/H265VideoRTPSource.cpp                                  \
	live/liveMedia/H265VideoStreamDiscreteFramer.cpp                       \
	live/liveMedia/H265VideoStreamFramer.cpp                               \
	live/liveMedia/InputFile.cpp                                           \
	live/liveMedia/JPEGVideoRTPSink.cpp                                    \
	live/liveMedia/JPEGVideoRTPSource.cpp                                  \
	live/liveMedia/JPEGVideoSource.cpp                                     \
	live/liveMedia/Locale.cpp                                              \
	live/liveMedia/MatroskaDemuxedTrack.cpp                                \
	live/liveMedia/MatroskaFile.cpp                                        \
	live/liveMedia/MatroskaFileParser.cpp                                  \
	live/liveMedia/MatroskaFileServerDemux.cpp                             \
	live/liveMedia/MatroskaFileServerMediaSubsession.cpp                   \
	live/liveMedia/Media.cpp                                               \
	live/liveMedia/MediaSession.cpp                                        \
	live/liveMedia/MediaSink.cpp                                           \
	live/liveMedia/MediaSource.cpp                                         \
	live/liveMedia/MP3ADU.cpp                                              \
	live/liveMedia/MP3ADUdescriptor.cpp                                    \
	live/liveMedia/MP3ADUinterleaving.cpp                                  \
	live/liveMedia/MP3ADURTPSink.cpp                                       \
	live/liveMedia/MP3ADURTPSource.cpp                                     \
	live/liveMedia/MP3ADUTranscoder.cpp                                    \
	live/liveMedia/MP3AudioFileServerMediaSubsession.cpp                   \
	live/liveMedia/MP3AudioMatroskaFileServerMediaSubsession.cpp           \
	live/liveMedia/MP3FileSource.cpp                                       \
	live/liveMedia/MP3Internals.cpp                                        \
	live/liveMedia/MP3InternalsHuffman.cpp                                 \
	live/liveMedia/MP3InternalsHuffmanTable.cpp                            \
	live/liveMedia/MP3StreamState.cpp                                      \
	live/liveMedia/MP3Transcoder.cpp                                       \
	live/liveMedia/MPEG1or2AudioRTPSink.cpp                                \
	live/liveMedia/MPEG1or2AudioRTPSource.cpp                              \
	live/liveMedia/MPEG1or2AudioStreamFramer.cpp                           \
	live/liveMedia/MPEG1or2Demux.cpp                                       \
	live/liveMedia/MPEG1or2DemuxedElementaryStream.cpp                     \
	live/liveMedia/MPEG1or2DemuxedServerMediaSubsession.cpp                \
	live/liveMedia/MPEG1or2FileServerDemux.cpp                             \
	live/liveMedia/MPEG1or2VideoFileServerMediaSubsession.cpp              \
	live/liveMedia/MPEG1or2VideoRTPSink.cpp                                \
	live/liveMedia/MPEG1or2VideoRTPSource.cpp                              \
	live/liveMedia/MPEG1or2VideoStreamDiscreteFramer.cpp                   \
	live/liveMedia/MPEG1or2VideoStreamFramer.cpp                           \
	live/liveMedia/MPEG2IndexFromTransportStream.cpp                       \
	live/liveMedia/MPEG2TransportFileServerMediaSubsession.cpp             \
	live/liveMedia/MPEG2TransportStreamFramer.cpp                          \
	live/liveMedia/MPEG2TransportStreamFromESSource.cpp                    \
	live/liveMedia/MPEG2TransportStreamFromPESSource.cpp                   \
	live/liveMedia/MPEG2TransportStreamIndexFile.cpp                       \
	live/liveMedia/MPEG2TransportStreamMultiplexor.cpp                     \
	live/liveMedia/MPEG2TransportStreamTrickModeFilter.cpp                 \
	live/liveMedia/MPEG2TransportUDPServerMediaSubsession.cpp              \
	live/liveMedia/MPEG4ESVideoRTPSink.cpp                                 \
	live/liveMedia/MPEG4ESVideoRTPSource.cpp                               \
	live/liveMedia/MPEG4GenericRTPSink.cpp                                 \
	live/liveMedia/MPEG4GenericRTPSource.cpp                               \
	live/liveMedia/MPEG4LATMAudioRTPSink.cpp                               \
	live/liveMedia/MPEG4LATMAudioRTPSource.cpp                             \
	live/liveMedia/MPEG4VideoFileServerMediaSubsession.cpp                 \
	live/liveMedia/MPEG4VideoStreamDiscreteFramer.cpp                      \
	live/liveMedia/MPEG4VideoStreamFramer.cpp                              \
	live/liveMedia/MPEGVideoStreamFramer.cpp                               \
	live/liveMedia/MPEGVideoStreamParser.cpp                               \
	live/liveMedia/MultiFramedRTPSink.cpp                                  \
	live/liveMedia/MultiFramedRTPSource.cpp                                \
	live/liveMedia/OggDemuxedTrack.cpp                                     \
	live/liveMedia/OggFile.cpp                                             \
	live/liveMedia/OggFileParser.cpp                                       \
	live/liveMedia/OggFileServerDemux.cpp                                  \
	live/liveMedia/OggFileServerMediaSubsession.cpp                        \
	live/liveMedia/OggFileSink.cpp                                         \
	live/liveMedia/OnDemandServerMediaSubsession.cpp                       \
	live/liveMedia/ourMD5.cpp                                              \
	live/liveMedia/OutputFile.cpp                                          \
	live/liveMedia/PassiveServerMediaSubsession.cpp                        \
	live/liveMedia/ProxyServerMediaSession.cpp                             \
	live/liveMedia/QCELPAudioRTPSource.cpp                                 \
	live/liveMedia/QuickTimeFileSink.cpp                                   \
	live/liveMedia/QuickTimeGenericRTPSource.cpp                           \
	live/liveMedia/RTCP.cpp                                                \
	live/liveMedia/rtcp_from_spec.c                                        \
	live/liveMedia/RTPInterface.cpp                                        \
	live/liveMedia/RTPSink.cpp                                             \
	live/liveMedia/RTPSource.cpp                                           \
	live/liveMedia/RTSPClient.cpp                                          \
	live/liveMedia/RTSPCommon.cpp                                          \
	live/liveMedia/RTSPRegisterSender.cpp                                  \
	live/liveMedia/RTSPServer.cpp                                          \
	live/liveMedia/RTSPServerSupportingHTTPStreaming.cpp                   \
	live/liveMedia/ServerMediaSession.cpp                                  \
	live/liveMedia/SimpleRTPSink.cpp                                       \
	live/liveMedia/SimpleRTPSource.cpp                                     \
	live/liveMedia/SIPClient.cpp                                           \
	live/liveMedia/StreamParser.cpp                                        \
	live/liveMedia/StreamReplicator.cpp                                    \
	live/liveMedia/T140TextRTPSink.cpp                                     \
	live/liveMedia/TCPStreamSink.cpp                                       \
	live/liveMedia/TextRTPSink.cpp                                         \
	live/liveMedia/TheoraVideoRTPSink.cpp                                  \
	live/liveMedia/TheoraVideoRTPSource.cpp                                \
	live/liveMedia/uLawAudioFilter.cpp                                     \
	live/liveMedia/VideoRTPSink.cpp                                        \
	live/liveMedia/VorbisAudioRTPSink.cpp                                  \
	live/liveMedia/VorbisAudioRTPSource.cpp                                \
	live/liveMedia/VP8VideoRTPSink.cpp                                     \
	live/liveMedia/VP8VideoRTPSource.cpp                                   \
	live/liveMedia/VP9VideoRTPSink.cpp                                     \
	live/liveMedia/VP9VideoRTPSource.cpp                                   \
	live/liveMedia/WAVAudioFileServerMediaSubsession.cpp                   \
	live/liveMedia/WAVAudioFileSource.cpp                                  

LOCAL_C_INCLUDES += \
	$(LOCAL_PATH)/live/liveMedia/include \
	$(LOCAL_PATH)/live/BasicUsageEnvironment/include \
	$(LOCAL_PATH)/live/groupsock/include \
	$(LOCAL_PATH)/live/UsageEnvironment/include \
	$(LOCAL_PATH)/include

LOCAL_LDLIBS += -llog  


LOCAL_CPPFLAGS += -DNULL=0 -DNO_SSTREAM=1  -fexceptions -DXLOCALE_NOT_USED=1

#  -UIP_ADD_SOURCE_MEMBERSHIP  
#include <xlocale.h> // because, on some systems, <locale.h> doesn't include <xlocale.h>; this makes sure that we get both
   
include $(BUILD_SHARED_LIBRARY)  

