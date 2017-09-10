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
// A generic media server class, used to implement a RTSP server, and any other server that uses
//  "ServerMediaSession" objects to describe media to be served.
// Implementation

#include "GenericMediaServer.hh"
#include <GroupsockHelper.hh>
#if defined(__WIN32__) || defined(_WIN32) || defined(_QNX4)
#define snprintf _snprintf
#endif

#define DEBUG

////////// GenericMediaServer implementation //////////

void GenericMediaServer::addServerMediaSession(ServerMediaSession* serverMediaSession) {
  if (serverMediaSession == NULL) return;

  // 
  char const* sessionName = serverMediaSession->streamName();
  if (sessionName == NULL) sessionName = "";
  removeServerMediaSession(sessionName); // in case an existing "ServerMediaSession" with this name already exists

  // 用hashTable 键值对方式<uri名字,ServerMediaSesssion实例> 保存一个session 对应一个url 
  // incomingConnectionHandler accept客户端
  fServerMediaSessions->Add(sessionName, (void*)serverMediaSession);
}

ServerMediaSession* GenericMediaServer
::lookupServerMediaSession(char const* streamName, Boolean /*isFirstLookupInSession*/) {
  // Default implementation: 从hashTable中根据名字获得 ServerMediaSession 实例
  return (ServerMediaSession*)(fServerMediaSessions->Lookup(streamName));
}

void GenericMediaServer::removeServerMediaSession(ServerMediaSession* serverMediaSession) {
  if (serverMediaSession == NULL) return;
  
  fServerMediaSessions->Remove(serverMediaSession->streamName());// 从HashTable中移除 
  if (serverMediaSession->referenceCount() == 0) {
    Medium::close(serverMediaSession);// 如果还没有引用的话 析构ServerMediaSession
  } else { // 引用计数为1 >0 而且从Table中已经移除
    serverMediaSession->deleteWhenUnreferenced() = True; 
	// 如果还有引用 但HashTable已移除记录 那么设置这个ServerMediaSession可删除
  }
  // ClientSession::~ClientSession 客户断开这次会话 ServerMediaSession如果之前没有设置 
  // deleteWhenUnreferenced() 那么这个 ServerMediaSession不会被从HashTable中移除
}

void GenericMediaServer::removeServerMediaSession(char const* streamName) {
  removeServerMediaSession((ServerMediaSession*)(fServerMediaSessions->Lookup(streamName)));
}

void GenericMediaServer::closeAllClientSessionsForServerMediaSession(ServerMediaSession* serverMediaSession) {
  if (serverMediaSession == NULL) return;
  
  HashTable::Iterator* iter = HashTable::Iterator::create(*fClientSessions);
  GenericMediaServer::ClientSession* clientSession;
  char const* key; // dummy
  while ((clientSession = (GenericMediaServer::ClientSession*)(iter->next(key))) != NULL) {
    if (clientSession->fOurServerMediaSession == serverMediaSession) {
      delete clientSession;
    }
  }
  delete iter;
}

void GenericMediaServer::closeAllClientSessionsForServerMediaSession(char const* streamName) {
  closeAllClientSessionsForServerMediaSession((ServerMediaSession*)(fServerMediaSessions->Lookup(streamName)));
}

void GenericMediaServer::deleteServerMediaSession(ServerMediaSession* serverMediaSession) {
  if (serverMediaSession == NULL) return;
  
  closeAllClientSessionsForServerMediaSession(serverMediaSession);
  removeServerMediaSession(serverMediaSession);
}

void GenericMediaServer::deleteServerMediaSession(char const* streamName) {
  deleteServerMediaSession((ServerMediaSession*)(fServerMediaSessions->Lookup(streamName)));
}

GenericMediaServer
::GenericMediaServer(UsageEnvironment& env, int ourSocket, Port ourPort,
		     unsigned reclamationSeconds)
  : Medium(env),
    fServerSocket(ourSocket), fServerPort(ourPort), fReclamationSeconds(reclamationSeconds),
    fServerMediaSessions(HashTable::create(STRING_HASH_KEYS)),
    fClientConnections(HashTable::create(ONE_WORD_HASH_KEYS)),
    fClientSessions(HashTable::create(STRING_HASH_KEYS)) {
  ignoreSigPipeOnSocket(fServerSocket); // so that clients on the same host that are killed don't also kill us

  // 监听客户的连接请求 accpet
  // Arrange to handle connections from others:
  env.taskScheduler().turnOnBackgroundReadHandling(fServerSocket, incomingConnectionHandler, this);
}

GenericMediaServer::~GenericMediaServer() {
  // Turn off background read handling:
  envir().taskScheduler().turnOffBackgroundReadHandling(fServerSocket);
  ::closeSocket(fServerSocket);
}

void GenericMediaServer::cleanup() {
  // This member function must be called in the destructor of any subclass of
  // "GenericMediaServer".  (We don't call this in the destructor of "GenericMediaServer" itself,
  // because by that time, the subclass destructor will already have been called, and this may
  // affect (break) the destruction of the "ClientSession" and "ClientConnection" objects, which
  // themselves will have been subclassed.)

  // Close all client session objects:
  GenericMediaServer::ClientSession* clientSession;
  while ((clientSession = (GenericMediaServer::ClientSession*)fClientSessions->getFirst()) != NULL) {
    delete clientSession;
  }
  delete fClientSessions;
  
  // Close all client connection objects:
  GenericMediaServer::ClientConnection* connection;
  while ((connection = (GenericMediaServer::ClientConnection*)fClientConnections->getFirst()) != NULL) {
    delete connection;
  }
  delete fClientConnections;
  
  // Delete all server media sessions
  ServerMediaSession* serverMediaSession;
  while ((serverMediaSession = (ServerMediaSession*)fServerMediaSessions->getFirst()) != NULL) {
    removeServerMediaSession(serverMediaSession); // will delete it, because it no longer has any 'client session' objects using it
  }
  delete fServerMediaSessions;
}

#define LISTEN_BACKLOG_SIZE 20

int GenericMediaServer::setUpOurSocket(UsageEnvironment& env, Port& ourPort) {
  int ourSocket = -1;
  
  do {
    // The following statement is enabled by default.
    // Don't disable it (by defining ALLOW_SERVER_PORT_REUSE) unless you know what you're doing.
#if !defined(ALLOW_SERVER_PORT_REUSE) && !defined(ALLOW_RTSP_SERVER_PORT_REUSE)
    // ALLOW_RTSP_SERVER_PORT_REUSE is for backwards-compatibility #####
    NoReuse dummy(env); // Don't use this socket if there's already a local server using it
#endif

	// 创建面向流的socket rtsp传输用tcp
    ourSocket = setupStreamSocket(env, ourPort);
    if (ourSocket < 0) break;
    
    // Make sure we have a big send buffer:
    // 增加发送的buffer  这里没有设置 SO_RCVBUF 接收的buffer
    // if (setsockopt(socket, SOL_SOCKET, SO_SNDBUF, (char*)&requestedSize, sizeSize)
    if (!increaseSendBufferTo(env, ourSocket, 50*1024)) break;
    
    // Allow multiple simultaneous connections:  最多20在listen队列
    if (listen(ourSocket, LISTEN_BACKLOG_SIZE) < 0) {
      env.setResultErrMsg("listen() failed: ");
      break;
    }
    
    if (ourPort.num() == 0) {
      // bind() will have chosen a port for us; return it also:
      if (!getSourcePort(env, ourSocket, ourPort)) break;
    }
    
    return ourSocket;
  } while (0);
  
  if (ourSocket != -1) ::closeSocket(ourSocket);
  return -1;
}

void GenericMediaServer::incomingConnectionHandler(void* instance, int /*mask*/) {
  GenericMediaServer* server = (GenericMediaServer*)instance;
  server->incomingConnectionHandler();
}
void GenericMediaServer::incomingConnectionHandler() {
  incomingConnectionHandlerOnSocket(fServerSocket);
}

void GenericMediaServer::incomingConnectionHandlerOnSocket(int serverSocket) {
  struct sockaddr_in clientAddr;
  SOCKLEN_T clientAddrLen = sizeof clientAddr;
  int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);
  if (clientSocket < 0) {
    int err = envir().getErrno();
    if (err != EWOULDBLOCK) {
      envir().setResultErrMsg("accept() failed: ");
    }
    return;
  }

  // 设置与新客户端连接socket的属性  比如 nonBlocking 发送buffer大小50*1024
  ignoreSigPipeOnSocket(clientSocket); // so that clients on the same host that are killed don't also kill us
  makeSocketNonBlocking(clientSocket);
  increaseSendBufferTo(envir(), clientSocket, 50*1024);
  
#ifdef DEBUG
  envir() << "accept()ed connection from " << AddressString(clientAddr).val() << "\n";
#endif
  
  // Create a new object for handling this connection:
  (void)createNewClientConnection(clientSocket, clientAddr);
}


////////// GenericMediaServer::ClientConnection implementation //////////

GenericMediaServer::ClientConnection
::ClientConnection(GenericMediaServer& ourServer, int clientSocket, struct sockaddr_in clientAddr)
  : fOurServer(ourServer), fOurSocket(clientSocket), fClientAddr(clientAddr) {
  // Add ourself to our 'client connections' table:
  fOurServer.fClientConnections->Add((char const*)this, this);
  
  // Arrange to handle incoming requests:
  resetRequestBuffer();
  envir().taskScheduler()
    .setBackgroundHandling(fOurSocket, SOCKET_READABLE|SOCKET_EXCEPTION, incomingRequestHandler, this);
}

GenericMediaServer::ClientConnection::~ClientConnection() {
  // Remove ourself from the server's 'client connections' hash table before we go:
  fOurServer.fClientConnections->Remove((char const*)this);
  
  closeSockets();
}

void GenericMediaServer::ClientConnection::closeSockets() {
  // Turn off background handling on our socket:
  envir().taskScheduler().disableBackgroundHandling(fOurSocket);
  ::closeSocket(fOurSocket);

  fOurSocket = -1;
}

void GenericMediaServer::ClientConnection::incomingRequestHandler(void* instance, int /*mask*/) {
  ClientConnection* connection = (ClientConnection*)instance;
  connection->incomingRequestHandler();
}

void GenericMediaServer::ClientConnection::incomingRequestHandler() {
  struct sockaddr_in dummy; // 'from' address, meaningless in this case

  // recvfrom可用于接收tcp(addr设置为NULL)和udp
  int bytesRead = readSocket(envir(), fOurSocket, &fRequestBuffer[fRequestBytesAlreadySeen], fRequestBufferBytesLeft, dummy);
  handleRequestBytes(bytesRead);
}

void GenericMediaServer::ClientConnection::resetRequestBuffer() {
  fRequestBytesAlreadySeen = 0;
  fRequestBufferBytesLeft = sizeof fRequestBuffer;
}


////////// GenericMediaServer::ClientSession implementation //////////

GenericMediaServer::ClientSession
::ClientSession(GenericMediaServer& ourServer, u_int32_t sessionId)
  : fOurServer(ourServer), fOurSessionId(sessionId), fOurServerMediaSession(NULL),
    fLivenessCheckTask(NULL) {
  noteLiveness(); // 这样可能启动心跳机制
}

GenericMediaServer::ClientSession::~ClientSession() {
  // Turn off any liveness checking:
  envir().taskScheduler().unscheduleDelayedTask(fLivenessCheckTask);

  // Remove ourself from the server's 'client sessions' hash table before we go:
  char sessionIdStr[8+1];
  sprintf(sessionIdStr, "%08X", fOurSessionId);
  fOurServer.fClientSessions->Remove(sessionIdStr);
  
  if (fOurServerMediaSession != NULL) { 
  	// 客户端断开的时候 会对ServerMediaSession减少引用 
  	// 如果 ServerMediaSession 引用为0的话，就会从RtspServer的HashTable中移除(关联的RtpSink FrameSource也会删除)
  	// 下一次有客户要连接该streamName 就重新建 ServerMediaSession 
    fOurServerMediaSession->decrementReferenceCount();
    if (fOurServerMediaSession->referenceCount() == 0
	&& fOurServerMediaSession->deleteWhenUnreferenced()) {
	// 如果引用数为0 但是 没有设置 deleteWhenUnreferenced = true 那么 不会从table中删除
      fOurServer.removeServerMediaSession(fOurServerMediaSession);
      fOurServerMediaSession = NULL;
    }
  }
}

void GenericMediaServer::ClientSession::noteLiveness() {
#ifdef DEBUG
  char const* streamName
    = (fOurServerMediaSession == NULL) ? "???" : fOurServerMediaSession->streamName();
  fprintf(stderr, "Client session (id \"%08X\", stream name \"%s\"): Liveness indication\n",
	  fOurSessionId, streamName);
#endif

/*
	ServerMediaSession默认的noteLiveness是空的
	但是 RTSPClientSession 的noteLiveness是会关闭传输的

	fReclamationTestSeconds在RTPServer构造时传入，默认为65s
	表示如65s内未收到客户端RTCP包即认为客户端已断开 

	如果在fReclamationTestSeconds的时间内再次调用noteLiveness
	则该延迟任务会被设置成新的时间，原来的调度不再起作用

*/
  if (fOurServerMediaSession != NULL) fOurServerMediaSession->noteLiveness();

  if (fOurServer.fReclamationSeconds > 0) {
    envir().taskScheduler().rescheduleDelayedTask(fLivenessCheckTask,
						  fOurServer.fReclamationSeconds*1000000,
						  (TaskFunc*)livenessTimeoutTask, this);
  }
}

void GenericMediaServer::ClientSession::noteClientLiveness(ClientSession* clientSession) {

  fprintf(stderr, "noteClientLiveness  ");

  clientSession->noteLiveness();
}

void GenericMediaServer::ClientSession::livenessTimeoutTask(ClientSession* clientSession) {
  // If this gets called, the client session is assumed to have timed out, so delete it:
#ifdef DEBUG
  char const* streamName
    = (clientSession->fOurServerMediaSession == NULL) ? "???" : clientSession->fOurServerMediaSession->streamName();
  fprintf(stderr, "Client session (id \"%08X\", stream name \"%s\") has timed out (due to inactivity)\n",
	  clientSession->fOurSessionId, streamName);
#endif
  delete clientSession;
}

GenericMediaServer::ClientSession* GenericMediaServer::createNewClientSessionWithId() {
  u_int32_t sessionId;
  char sessionIdStr[8+1];

  // Choose a random (unused) 32-bit integer for the session id
  // (it will be encoded as a 8-digit hex number).  (We avoid choosing session id 0,
  // because that has a special use by some servers.)
  do {
    sessionId = (u_int32_t)our_random32(); // 产生一个会话ID
    snprintf(sessionIdStr, sizeof sessionIdStr, "%08X", sessionId);
  } while (sessionId == 0 || lookupClientSession(sessionIdStr) != NULL);

  ClientSession* clientSession = createNewClientSession(sessionId);
  fClientSessions->Add(sessionIdStr, clientSession); 
  // rstp服务器实例fClientSessions保存所有ClientSession实例

  return clientSession;
}

GenericMediaServer::ClientSession*
GenericMediaServer::lookupClientSession(u_int32_t sessionId) {
  char sessionIdStr[8+1];
  snprintf(sessionIdStr, sizeof sessionIdStr, "%08X", sessionId);
  return lookupClientSession(sessionIdStr);
}

GenericMediaServer::ClientSession*
GenericMediaServer::lookupClientSession(char const* sessionIdStr) {
  return (GenericMediaServer::ClientSession*)fClientSessions->Lookup(sessionIdStr);
}


////////// ServerMediaSessionIterator implementation //////////

GenericMediaServer::ServerMediaSessionIterator
::ServerMediaSessionIterator(GenericMediaServer& server)
  : fOurIterator((server.fServerMediaSessions == NULL)
		 ? NULL : HashTable::Iterator::create(*server.fServerMediaSessions)) {
}

GenericMediaServer::ServerMediaSessionIterator::~ServerMediaSessionIterator() {
  delete fOurIterator;
}

ServerMediaSession* GenericMediaServer::ServerMediaSessionIterator::next() {
  if (fOurIterator == NULL) return NULL;

  char const* key; // dummy
  return (ServerMediaSession*)(fOurIterator->next(key));
}


////////// UserAuthenticationDatabase implementation //////////

UserAuthenticationDatabase::UserAuthenticationDatabase(char const* realm,
						       Boolean passwordsAreMD5)
  : fTable(HashTable::create(STRING_HASH_KEYS)),
    fRealm(strDup(realm == NULL ? "LIVE555 Streaming Media" : realm)),
    fPasswordsAreMD5(passwordsAreMD5) {
}

UserAuthenticationDatabase::~UserAuthenticationDatabase() {
  delete[] fRealm;
  
  // Delete the allocated 'password' strings that we stored in the table, and then the table itself:
  char* password;
  while ((password = (char*)fTable->RemoveNext()) != NULL) {
    delete[] password;
  }
  delete fTable;
}

void UserAuthenticationDatabase::addUserRecord(char const* username,
					       char const* password) {
  fTable->Add(username, (void*)(strDup(password)));
}

void UserAuthenticationDatabase::removeUserRecord(char const* username) {
  char* password = (char*)(fTable->Lookup(username));
  fTable->Remove(username);
  delete[] password;
}

char const* UserAuthenticationDatabase::lookupPassword(char const* username) {
  return (char const*)(fTable->Lookup(username));
}
