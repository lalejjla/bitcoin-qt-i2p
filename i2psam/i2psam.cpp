// Copyright (c) 2012-2013 giv
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//--------------------------------------------------------------------------------------------------
#include "i2psam.h"

#include <iostream>
#include <stdio.h>
#include <string.h>         // for memset
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>

#ifndef WIN32
#include <errno.h>
#endif

#ifndef WIN32
#define closesocket         close
#endif

#define SAM_BUFSIZE         65536
#define I2P_DESTINATION_SIZE 516

namespace SAM
{

static void print_error(const std::string& err)
{
#ifdef WIN32
    std::cout << err << "(" << WSAGetLastError() << ")" << std::endl;
#else
    std::cout << err << "(" << errno << ")" << std::endl;
#endif
}

#ifdef WIN32
int Socket::instances_ = 0;

void Socket::initWSA()
{
    WSADATA wsadata;
    int ret = WSAStartup(MAKEWORD(2,2), &wsadata);
    if (ret != NO_ERROR)
        print_error("Failed to initialize winsock library");
}

void Socket::freeWSA()
{
    WSACleanup();
}
#endif

Socket::Socket(const std::string& SAMHost, uint16_t SAMPort, const std::string& minVer, const std::string &maxVer)
    : socket_(SAM_INVALID_SOCKET), SAMHost_(SAMHost), SAMPort_(SAMPort), minVer_(minVer), maxVer_(maxVer)
{
#ifdef WIN32
    if (instances_++ == 0)
        initWSA();
#endif

    memset(&servAddr_, 0, sizeof(servAddr_));

    servAddr_.sin_family = AF_INET;
    servAddr_.sin_addr.s_addr = inet_addr(SAMHost.c_str());
    servAddr_.sin_port = htons(SAMPort);

    init();
    if (isOk())
        handshake();
}

Socket::Socket(const sockaddr_in& addr, const std::string &minVer, const std::string& maxVer)
    : socket_(SAM_INVALID_SOCKET), servAddr_(addr), minVer_(minVer), maxVer_(maxVer)
{
#ifdef WIN32
    if (instances_++ == 0)
        initWSA();
#endif

    init();
    if (isOk())
        handshake();
}

Socket::Socket(const Socket& rhs)
    : socket_(SAM_INVALID_SOCKET), servAddr_(rhs.servAddr_), minVer_(rhs.minVer_), maxVer_(rhs.maxVer_)
{
#ifdef WIN32
    if (instances_++ == 0)
        initWSA();
#endif

    init();
    if (isOk())
        handshake();
}

Socket::~Socket()
{
    close();

#ifdef WIN32
    if (--instances_ == 0)
        freeWSA();
#endif
}

void Socket::init()
{
    socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_ == SAM_INVALID_SOCKET)
    {
        print_error("Failed to create socket");
        return;
    }

    if (connect(socket_, (const sockaddr*)&servAddr_, sizeof(servAddr_)) == SAM_SOCKET_ERROR)
    {
        close();
        print_error("Failed to connect to SAM");
        return;
    }
}

SOCKET Socket::release()
{
    SOCKET temp = socket_;
    socket_ = SAM_INVALID_SOCKET;
    return temp;
}

void Socket::handshake()
{
    this->write(Message::hello(minVer_, maxVer_));
    const std::string answer = this->read();
    const Message::eStatus answerStatus = Message::checkAnswer(answer);
    if (answerStatus == Message::OK)
        version_ = Message::getValue(answer, "VERSION");
    else
        print_error("Handshake failed");
}

void Socket::write(const std::string& msg)
{
    if (!isOk())
    {
        print_error("Failed to send data because socket is closed");
        return;
    }
    std::cout << "Send: " << msg << std::endl;
    ssize_t sentBytes = send(socket_, msg.c_str(), msg.length(), 0);
    if (sentBytes == SAM_SOCKET_ERROR)
    {
        close();
        print_error("Failed to send data");
        return;
    }
    if (sentBytes == 0)
    {
        close();
        print_error("Socket was closed");
        return;
    }
}

std::string Socket::read()
{
    if (!isOk())
    {
        print_error("Failed to read data because socket is closed");
        return std::string();
    }
    char buffer[SAM_BUFSIZE];
    memset(buffer, 0, SAM_BUFSIZE);
    ssize_t recievedBytes = recv(socket_, buffer, SAM_BUFSIZE, 0);
    if (recievedBytes == SAM_SOCKET_ERROR)
    {
        close();
        print_error("Failed to receive data");
        return std::string();
    }
    if (recievedBytes == 0)
    {
        close();
        print_error("Socket was closed");
    }
    std::cout << "Reply: " << buffer << std::endl;
    return std::string(buffer);
}

void Socket::close()
{
    if (socket_ != SAM_INVALID_SOCKET)
        ::closesocket(socket_);
    socket_ = SAM_INVALID_SOCKET;
}

bool Socket::isOk() const
{
    return socket_ != SAM_INVALID_SOCKET;
}

const std::string& Socket::getHost() const
{
    return SAMHost_;
}

uint16_t Socket::getPort() const
{
    return SAMPort_;
}

const std::string& Socket::getVersion() const
{
    return version_;
}

const std::string& Socket::getMinVer() const
{
    return minVer_;
}

const std::string& Socket::getMaxVer() const
{
    return maxVer_;
}

const sockaddr_in& Socket::getAddress() const
{
    return servAddr_;
}


//--------------------------------------------------------------------------------------------------

NewStreamSession::NewStreamSession(
        const std::string& nickname,
        const std::string& SAMHost     /*= SAM_DEFAULT_ADDRESS*/,
              uint16_t     SAMPort     /*= SAM_DEFAULT_PORT*/,
        const std::string& destination /*= SAM_GENERATE_MY_DESTINATION*/,
        const std::string& i2pOptions  /*= SAM_DEFAULT_I2P_OPTIONS*/,
        const std::string& minVer      /*= SAM_DEFAULT_MIN_VER*/,
        const std::string& maxVer      /*= SAM_DEFAULT_MAX_VER*/)
    : socket_(SAMHost, SAMPort, minVer, maxVer)
    , nickname_(nickname)
    , sessionID_(generateSessionID())
//    , myDestination_(myDestination)
    , i2pOptions_(i2pOptions)
//    , isDestGenerated_(myDestination == SAM_GENERATE_MY_DESTINATION)
    , isSick_(false)
{
    myDestination_ = createStreamSession(destination);
}

NewStreamSession::NewStreamSession(NewStreamSession& rhs)
    : socket_(rhs.socket_)
    , nickname_(rhs.nickname_)
    , sessionID_(generateSessionID())
    , myDestination_(rhs.myDestination_)
    , i2pOptions_(rhs.i2pOptions_)
//    , isDestGenerated_(rhs.isDestGenerated_)
    , isSick_(false)
{
    rhs.fallSick();
    rhs.socket_.close();
    (void)createStreamSession(myDestination_.priv);

    for(ForwardedStreamsContainer::const_iterator it = rhs.forwardedStreams_.begin(), end = rhs.forwardedStreams_.end(); it != end; ++it)
        forward(it->host, it->port, it->silent);
}

NewStreamSession::~NewStreamSession()
{
    stopForwardingAll();
    std::cout << "Closing SAM session..." << std::endl;
}

/*static*/
std::string NewStreamSession::generateSessionID()
{
    static const int minSessionIDLength = 5;
    static const int maxSessionIDLength = 9;
    static const char sessionIDAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int length = minSessionIDLength - 1;
    std::string result;

    srand(time(NULL));

    while(length < minSessionIDLength)
        length = rand() % maxSessionIDLength;

    while (length-- > 0)
        result += sessionIDAlphabet[rand() % (sizeof(sessionIDAlphabet)-1)];

    return result;
}

RequestResult<std::auto_ptr<Socket> > NewStreamSession::accept(bool silent)
{
    typedef RequestResult<std::auto_ptr<Socket> > ResultType;

    std::auto_ptr<Socket> streamSocket(new Socket(socket_));
    const Message::eStatus status = accept(*streamSocket, sessionID_, silent);
    switch(status)
    {
    case Message::OK:
        return RequestResult<std::auto_ptr<Socket> >(streamSocket);
    case Message::EMPTY_ANSWER:
    case Message::CLOSED_SOCKET:
    case Message::INVALID_ID:
        fallSick();
        break;
    default:
        break;
    }
    return ResultType();
}

RequestResult<std::auto_ptr<Socket> > NewStreamSession::connect(const std::string& destination, bool silent)
{
    typedef RequestResult<std::auto_ptr<Socket> > ResultType;

    std::auto_ptr<Socket> streamSocket(new Socket(socket_));
    const Message::eStatus status = connect(*streamSocket, sessionID_, destination, silent);
    switch(status)
    {
    case Message::OK:
        return ResultType(streamSocket);
    case Message::EMPTY_ANSWER:
    case Message::CLOSED_SOCKET:
    case Message::INVALID_ID:
        fallSick();
        break;
    default:
        break;
    }
    return ResultType();
}


RequestResult<void> NewStreamSession::forward(const std::string& host, uint16_t port, bool silent)
{
    typedef RequestResult<void> ResultType;

    std::auto_ptr<Socket> newSocket(new Socket(socket_));
    const Message::eStatus status = forward(*newSocket, sessionID_, host, port, silent);
    switch(status)
    {
    case Message::OK:
        forwardedStreams_.push_back(ForwardedStream(newSocket.get(), host, port, silent));
        newSocket.release();    // release after successful push_back only
        return ResultType(true);
    case Message::EMPTY_ANSWER:
    case Message::CLOSED_SOCKET:
    case Message::INVALID_ID:
        fallSick();
        break;
    default:
        break;
    }
    return ResultType();
}

RequestResult<const std::string> NewStreamSession::namingLookup(const std::string& name) const
{
    typedef RequestResult<const std::string> ResultType;
    typedef Message::Answer<const std::string> AnswerType;

    std::auto_ptr<Socket> newSocket(new Socket(socket_));
    const AnswerType answer = namingLookup(*newSocket, name);
    switch(answer.status)
    {
    case Message::OK:
        return ResultType(answer.value);
    case Message::EMPTY_ANSWER:
    case Message::CLOSED_SOCKET:
        fallSick();
        break;
    default:
        break;
    }
    return ResultType();
}


RequestResult<const FullDestination> NewStreamSession::destGenerate() const
{
    typedef RequestResult<const FullDestination> ResultType;
    typedef Message::Answer<const FullDestination> AnswerType;

    std::auto_ptr<Socket> newSocket(new Socket(socket_));
    const AnswerType answer = destGenerate(*newSocket);
    switch(answer.status)
    {
    case Message::OK:
        return ResultType(answer.value);
    case Message::EMPTY_ANSWER:
    case Message::CLOSED_SOCKET:
        fallSick();
        break;
    default:
        break;
    }
    return ResultType();
}

FullDestination NewStreamSession::createStreamSession(const std::string& destination)
{
    typedef Message::Answer<const std::string> AnswerType;

    const AnswerType answer = createStreamSession(socket_, sessionID_, nickname_, destination, i2pOptions_);
    if (answer.status != Message::OK)
    {
        fallSick();
        return FullDestination()/*ResultType()*/;
    }

    return FullDestination(answer.value.substr(0, I2P_DESTINATION_SIZE), answer.value, (destination == SAM_GENERATE_MY_DESTINATION));

//    myDestination_.pub = myDestination;
//    myDestination_.priv = answer.value;
//    myDestination_.isGenerated = (myDestination == SAM_GENERATE_MY_DESTINATION);
}

void NewStreamSession::fallSick() const
{
    isSick_ = true;
}

void NewStreamSession::stopForwarding(const std::string& host, uint16_t port)
{
    for (ForwardedStreamsContainer::iterator it = forwardedStreams_.begin(); it != forwardedStreams_.end(); )
    {
        if (it->port == port && it->host == host)
        {
            delete (it->socket);
            it = forwardedStreams_.erase(it);
        }
        else
            ++it;
    }
}

void NewStreamSession::stopForwardingAll()
{
    for (ForwardedStreamsContainer::iterator it = forwardedStreams_.begin(); it != forwardedStreams_.end(); )
        delete (it->socket);
    forwardedStreams_.clear();
}

/*static*/
Message::Answer<const std::string> NewStreamSession::rawRequest(Socket& socket, const std::string& requestStr)
{
    if (!socket.isOk())
        return Message::Answer<const std::string>(Message::CLOSED_SOCKET);
    socket.write(requestStr);
    const std::string answer = socket.read();
    const Message::eStatus status = Message::checkAnswer(answer);
    return Message::Answer<const std::string>(status, answer);
}

/*static*/
Message::Answer<const std::string> NewStreamSession::request(Socket& socket, const std::string& requestStr, const std::string& keyOnSuccess)
{
    const Message::Answer<const std::string> answer = rawRequest(socket, requestStr);
    return (answer.status == Message::OK) ?
                Message::Answer<const std::string>(answer.status, Message::getValue(answer.value, keyOnSuccess)) :
                answer;
}

/*static*/
Message::eStatus NewStreamSession::request(Socket& socket, const std::string& requestStr)
{
    return rawRequest(socket, requestStr).status;
}

/*static*/
Message::Answer<const std::string> NewStreamSession::createStreamSession(Socket& socket, const std::string& sessionID, const std::string& nickname, const std::string& destination, const std::string& options)
{
    return request(socket, Message::sessionCreate(Message::sssStream, sessionID, nickname, destination, options), "DESTINATION");
}

/*static*/
Message::Answer<const std::string> NewStreamSession::namingLookup(Socket& socket, const std::string& name)
{
    return request(socket, Message::namingLookup(name), "VALUE");
}

/*static*/
Message::Answer<const FullDestination> NewStreamSession::destGenerate(Socket& socket)
{
// while answer for a DEST GENERATE request doesn't contain a "RESULT" field we parse it manually
    typedef Message::Answer<const FullDestination> ResultType;

    if (!socket.isOk())
        return ResultType(Message::CLOSED_SOCKET, FullDestination());
    socket.write(Message::destGenerate());
    const std::string answer = socket.read();
    const std::string pub = Message::getValue(answer, "PUB");
    const std::string priv = Message::getValue(answer, "PRIV");
    return (!pub.empty() && !priv.empty()) ? ResultType(Message::OK, FullDestination(pub, priv, /*isGenerated*/ true)) : ResultType(Message::EMPTY_ANSWER, FullDestination());
}

/*static*/
Message::eStatus NewStreamSession::accept(Socket& socket, const std::string& sessionID, bool silent)
{
    return request(socket, Message::streamAccept(sessionID, silent));
}

/*static*/
Message::eStatus NewStreamSession::connect(Socket& socket, const std::string& sessionID, const std::string& destination, bool silent)
{
    return request(socket, Message::streamConnect(sessionID, destination, silent));
}

/*static*/
Message::eStatus NewStreamSession::forward(Socket& socket, const std::string& sessionID, const std::string& host, uint16_t port, bool silent)
{
    return request(socket, Message::streamForward(sessionID, host, port, silent));
}

const std::string& NewStreamSession::getNickname() const
{
    return nickname_;
}

const std::string& NewStreamSession::getSessionID() const
{
    return sessionID_;
}

//const std::string& NewStreamSession::getMyDestination() const
//{
//    return myDestination_;
//}

const std::string& NewStreamSession::getOptions() const
{
    return i2pOptions_;
}

//bool NewStreamSession::isDestGenerated() const
//{
//    return isDestGenerated_;
//}

const FullDestination& NewStreamSession::getMyDestination() const
{
    return myDestination_;
}

bool NewStreamSession::isSick() const
{
    return isSick_;
}

const sockaddr_in& NewStreamSession::getSAMAddress() const
{
    return socket_.getAddress();
}

const std::string& NewStreamSession::getSAMHost() const
{
    return socket_.getHost();
}

uint16_t NewStreamSession::getSAMPort() const
{
    return socket_.getPort();
}

const std::string& NewStreamSession::getSAMMinVer() const
{
    return socket_.getMinVer();
}

const std::string& NewStreamSession::getSAMMaxVer() const
{
    return socket_.getMaxVer();
}

const std::string& NewStreamSession::getSAMVersion() const
{
    return socket_.getVersion();
}

//--------------------------------------------------------------------------------------------------

class StreamSessionAdapter::SessionHolder
{
public:
    explicit SessionHolder(std::auto_ptr<NewStreamSession> session)
        : session_(session)
    {}

    const NewStreamSession& getSession() const
    {
        heal();
        return *session_;
    }

    NewStreamSession& getSession()
    {
        heal();
        return *session_;
    }

private:
    void heal() const
    {
        if (!session_->isSick())
            return;
        reborn(); // if we don't know how to heal it just reborn it
    }

    void reborn() const
    {
        std::auto_ptr<NewStreamSession> newSession(new NewStreamSession(*session_));
        if (!newSession->isSick() && session_->isSick())
            session_ = newSession;
    }

    mutable std::auto_ptr<NewStreamSession> session_;
};

StreamSessionAdapter::StreamSessionAdapter(
        const std::string& nickname,
        const std::string& SAMHost       /*= SAM_DEFAULT_ADDRESS*/,
              uint16_t     SAMPort       /*= SAM_DEFAULT_PORT*/,
        const std::string& myDestination /*= SAM_GENERATE_MY_DESTINATION*/,
        const std::string& i2pOptions    /*= SAM_DEFAULT_I2P_OPTIONS*/,
        const std::string& minVer        /*= SAM_DEFAULT_MIN_VER*/,
        const std::string& maxVer        /*= SAM_DEFAULT_MAX_VER*/)
    : sessionHolder_(
          new SessionHolder(
              std::auto_ptr<NewStreamSession>(
                  new NewStreamSession(nickname, SAMHost, SAMPort, myDestination, i2pOptions, minVer, maxVer))))
{}

SOCKET StreamSessionAdapter::accept(bool silent)
{
    RequestResult<std::auto_ptr<Socket> > result = sessionHolder_->getSession().accept(silent);
    // call Socket::release
    return result.isOk ? result.value->release() : SAM_INVALID_SOCKET;
}

SOCKET StreamSessionAdapter::connect(const std::string& destination, bool silent)
{
    RequestResult<std::auto_ptr<Socket> > result = sessionHolder_->getSession().connect(destination, silent);
    // call Socket::release
    return result.isOk ? result.value->release() : SAM_INVALID_SOCKET;
}

bool StreamSessionAdapter::forward(const std::string& host, uint16_t port, bool silent)
{
    return sessionHolder_->getSession().forward(host, port, silent).isOk;
}

std::string StreamSessionAdapter::namingLookup(const std::string& name) const
{
    RequestResult<const std::string> result = sessionHolder_->getSession().namingLookup(name);
    return result.isOk ? result.value : std::string();
}

FullDestination StreamSessionAdapter::destGenerate() const
{
    RequestResult<const FullDestination> result = sessionHolder_->getSession().destGenerate();
    return result.isOk ? result.value : FullDestination();
}

void StreamSessionAdapter::stopForwarding(const std::string& host, uint16_t port)
{
    sessionHolder_->getSession().stopForwarding(host, port);
}

void StreamSessionAdapter::stopForwardingAll()
{
    sessionHolder_->getSession().stopForwardingAll();
}

const FullDestination& StreamSessionAdapter::getMyDestination() const
{
    return sessionHolder_->getSession().getMyDestination();
}

const sockaddr_in& StreamSessionAdapter::getSAMAddress() const
{
    return sessionHolder_->getSession().getSAMAddress();
}

const std::string& StreamSessionAdapter::getSAMHost() const
{
    return sessionHolder_->getSession().getSAMHost();
}

uint16_t StreamSessionAdapter::getSAMPort() const
{
    return sessionHolder_->getSession().getSAMPort();
}

const std::string& StreamSessionAdapter::getNickname() const
{
    return sessionHolder_->getSession().getNickname();
}

const std::string& StreamSessionAdapter::getSAMMinVer() const
{
    return sessionHolder_->getSession().getSAMMinVer();
}

const std::string& StreamSessionAdapter::getSAMMaxVer() const
{
    return sessionHolder_->getSession().getSAMMaxVer();
}

const std::string& StreamSessionAdapter::getSAMVersion() const
{
    return sessionHolder_->getSession().getSAMVersion();
}

const std::string& StreamSessionAdapter::getOptions() const
{
    return sessionHolder_->getSession().getOptions();
}

//--------------------------------------------------------------------------------------------------

//std::string StreamSession::generateSessionID()
//{
//    static const int minSessionIDLength = 5;
//    static const int maxSessionIDLength = 9;
//    static const char sessionIDAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
//    int length = minSessionIDLength - 1;
//    std::string result;

//    srand(time(NULL));

//    while(length < minSessionIDLength)
//        length = rand() % maxSessionIDLength;

//    while (length-- > 0)
//        result += sessionIDAlphabet[rand() % (sizeof(sessionIDAlphabet)-1)];

//    return result;
//}

//StreamSession::StreamSession(
//        const std::string& nickname,
//        const std::string& SAMHost /*= SAM_DEFAULT_ADDRESS*/,
//        uint16_t SAMPort /*= SAM_DEFAULT_PORT*/,
//        const std::string& myDestination /*= SAM_GENERATE_MY_DESTINATION*/,
//        const std::string& i2pOptions /*= SAM_DEFAULT_I2P_OPTIONS*/,
//        const std::string& minVer /*= SAM_DEFAULT_MIN_VER*/,
//        const std::string& maxVer /*= SAM_DEFAULT_MAX_VER*/)
//    : socket_(new Socket(SAMHost, SAMPort, minVer, maxVer))/*,
//      reconnects_(0)*/

//{
//    (void)createStreamSession(socket_, nickname, myDestination, i2pOptions);
//}

//StreamSession::~StreamSession()
//{
//    for (ForwardedStreamsContainer::const_iterator it = forwardedStreams_.begin(), end = forwardedStreams_.end(); it != end; ++it)
//        delete (it->socket);
//    std::cout << "Closing SAM session..." << std::endl;
//}

//Message::Answer StreamSession::rawRequest(Socket& socket, const std::string& requestStr)
//{
//    if (!socket.isOk())
//        return Message::Answer(Message::CLOSED_SOCKET, std::string());
//    socket.write(requestStr);
//    const std::string answer = socket.read();
//    const Message::eStatus status = Message::checkAnswer(answer);
//    return Message::Answer(status, answer);
//}

//Message::Answer StreamSession::request(Socket& socket, const std::string& requestStr, const std::string& keyOnSuccess)
//{
//    Message::Answer answer = rawRequest(socket, requestStr);
//    if (status == Message::OK)
//        answer.value = Message::getValue(answer.value, keyOnSuccess);
//    return answer;


////    if (!socket.isOk())
////        return Message::Answer(Message::CLOSED_SOCKET, std::string());
////    socket.write(requestStr);
////    const std::string answer = socket.read();
////    const Message::eStatus status = Message::checkAnswer(answer);
////    return Message::Answer(
////                status,
////                (status == Message::OK) ? Message::getValue(answer, keyOnSuccess) : answer);
//}

//Message::eStatus StreamSession::request(Socket& socket, const std::string& requestStr)
//{
//    return rawRequest(socket, requestStr).status;

////    if (!socket.isOk())
////        return Message::CLOSED_SOCKET;
////    socket.write(requestStr);
////    const std::string answer = socket.read();
////    return Message::checkAnswer(answer);
//}

//Message::Answer StreamSession::createStreamSession(Socket& socket, const std::string& sessionID, const std::string& nickname, const std::string& destination, const std::string& options)
//{
//    return request(socket, Message::sessionCreate(Message::sssStream, sessionID, nickname, destination, options), "DESTINATION");
//}

//Message::Answer StreamSession::namingLookup(Socket& socket, const std::string& name)
//{
//    return request(socket, Message::namingLookup(name), "VALUE");
//}

//std::pair
//<
//    const Message::eStatus,
//    std::pair<const std::string, const std::string>
//>
//StreamSession::destGenerate(Socket &socket)
//{
//// while answer for a DEST GENERATE request doesn't contain a "RESULT" field we parse it manually

//    typedef std::pair<const std::string, const std::string> AnswerType;
//    typedef std::pair<const Message::eStatus, AnswerType> ResultType;

//    if (!socket.isOk())
//        return ResultType(Message::CLOSED_SOCKET, AnswerType());
//    socket.write(Message::destGenerate());
//    const std::string answer = socket.read();
//    const std::string pub = Message::getValue(answer, "PUB");
//    const std::string priv = Message::getValue(answer, "PRIV");
//    return (!pub.empty() && !priv.empty()) ? ResultType(Message::OK, AnswerType(pub, priv)) : ResultType(Message::EMPTY_ANSWER, AnswerType());
//}

//Message::Answer StreamSession::accept(Socket& socket, const std::string& sessionID, bool silent)
//{
//    return request(socket, Message::streamAccept(sessionID, silent), "");
//}

//Message::Answer StreamSession::connect(Socket& socket, const std::string& sessionID, const std::string& destination, bool silent)
//{
//    return request(socket, Message::streamConnect(sessionID, destination, silent), "");
//}

//Message::Answer StreamSession::forward(Socket& socket, const std::string& sessionID, const std::string& host, uint16_t port, bool silent)
//{
//    return request(socket, Message::streamForward(sessionID, host, port, silent), "");
//}

//bool StreamSession::createStreamSession(
//        std::auto_ptr<Socket>& newSocket,
//        const std::string& nickname,
//        const std::string& myDestination /*= SAM_GENERATE_MY_DESTINATION*/,
//        const std::string& i2pOptions /*= SAM_DEFAULT_I2P_OPTIONS*/)
//{
//    const std::string newSessionID = generateSessionID();
//    const Message::Answer result = createStreamSession(*newSocket, newSessionID, nickname, myDestination, i2pOptions);
//    switch(result.first)
//    {
//    case Message::OK:
//        break;
//    default:
//        return false;
//    }

//    nickname_ = nickname;
//    myDestination_ = result.second;
//    sessionID_ = newSessionID;
//    socket_ = newSocket;    // release and copy
//    i2pOptions_ = i2pOptions;
//    isGenerated_ = (myDestination == SAM_GENERATE_MY_DESTINATION);

//    return reforwardAll();
//}

//bool StreamSession::createStreamSession(
//        const std::string& nickname,
//        const std::string& SAMHost /*= SAM_DEFAULT_ADDRESS*/,
//        uint16_t SAMPort /*= SAM_DEFAULT_PORT*/,
//        const std::string& myDestination /*= SAM_GENERATE_MY_DESTINATION*/,
//        const std::string& i2pOptions /*= SAM_DEFAULT_I2P_OPTIONS*/,
//        const std::string& minVer, const std::string &maxVer /*= SAM_DEFAULT_MAX_VER*/)
//{
//    std::auto_ptr<Socket> newSocket(new Socket(SAMHost, SAMPort, minVer, maxVer));
//    return createStreamSession(newSocket, nickname, myDestination, i2pOptions);
//}

//bool StreamSession::createStreamSession()
//{
//    const Socket& currSocket = *socket_;
//    std::auto_ptr<Socket> newSocket(new Socket(currSocket));
//    return createStreamSession(newSocket, nickname_, (isGenerated_ ? SAM_GENERATE_MY_DESTINATION : myDestination_), i2pOptions_);
//}

//bool StreamSession::reforwardAll()
//{
//    for (ForwardedStreamsContainer::iterator it = forwardedStreams_.begin(), end = forwardedStreams_.end(); it != end; ++it)
//    {
//        std::auto_ptr<Socket> newSocket(new Socket(*socket_));
//        const Message::Answer result = forward(*newSocket, sessionID_, it->host, it->port, it->silent);
//        switch(result.first)
//        {
//        case Message::OK:
//            break;
//        default:
//            return false;
//        }

//        delete (it->socket);
//        it->socket = newSocket.release();
//    }
//    return true;
//}

//std::string StreamSession::namingLookup(const std::string& name)
//{
//    const Message::Answer result = namingLookup(*socket_, name);
//    switch(result.first)
//    {
//    case Message::OK:
//        return result.second;
//    case Message::EMPTY_ANSWER:
//    case Message::CLOSED_SOCKET:
//        return createStreamSession() ? namingLookup(name) : std::string();
//    default:
//        break;
//    }
//    return std::string();
//}

//std::string StreamSession::getMyAddress()
//{
////    return namingLookup(SAM_MY_NAME);
//    return myDestination_.substr(0, I2P_DESTINATION_SIZE);
//}

//std::pair<const std::string, const std::string> StreamSession::destGenerate()
//{
//    const std::pair<const Message::eStatus, std::pair<const std::string, const std::string> > result = destGenerate(*socket_);
//    switch(result.first)
//    {
//    case Message::OK:
//        return result.second;
//    case Message::EMPTY_ANSWER:
//    case Message::CLOSED_SOCKET:
//        return createStreamSession() ? destGenerate() : std::pair<const std::string, const std::string>();
//    default:
//        break;
//    }
//    return std::pair<const std::string, const std::string>();
//}

//SOCKET StreamSession::accept(bool silent /*= false*/)
//{
//    Socket streamSocket(*socket_);
//    const Message::Answer result = accept(streamSocket, sessionID_, silent);
//    switch(result.first)
//    {
//    case Message::OK:
//        return streamSocket.release();
//    case Message::EMPTY_ANSWER:
//    case Message::CLOSED_SOCKET:
//    case Message::INVALID_ID:
//        return createStreamSession() ? accept(silent) : SAM_INVALID_SOCKET;
//    default:
//        break;
//    }
//    return SAM_INVALID_SOCKET;
//}

//SOCKET StreamSession::connect(const std::string& destination, bool silent /*= false*/)
//{
//    Socket streamSocket(*socket_);
//    const Message::Answer result = connect(streamSocket, sessionID_, destination, silent);
//    switch(result.first)
//    {
//    case Message::OK:
//        return streamSocket.release();
//    case Message::EMPTY_ANSWER:
//    case Message::CLOSED_SOCKET:
//    case Message::INVALID_ID:
//        return createStreamSession() ? connect(destination, silent) : SAM_INVALID_SOCKET;
//    default:
//        break;
//    }
//    return SAM_INVALID_SOCKET;
//}

//bool StreamSession::forward(const std::string& host, uint16_t port, bool silent /*= false*/)
//{
//    std::auto_ptr<Socket> newSocket(new Socket(*socket_));
//    const Message::Answer result = forward(*newSocket, sessionID_, host, port, silent);
//    switch(result.first)
//    {
//    case Message::OK:
//        break;
//    case Message::EMPTY_ANSWER:
//    case Message::CLOSED_SOCKET:
//    case Message::INVALID_ID:
//        return createStreamSession() ? forward(host, port, silent) : false;
//    default:
//        return false;
//    }

//    ForwardedStream fwdStream;
//    fwdStream.host = host;
//    fwdStream.port = port;
//    fwdStream.silent = silent;
//    fwdStream.socket = newSocket.release();

//    forwardedStreams_.push_back(fwdStream);

//    return true;
//}

//void StreamSession::stopForwarding(const std::string& host, uint16_t port)
//{
//    for (ForwardedStreamsContainer::iterator it = forwardedStreams_.begin(); it != forwardedStreams_.end(); )
//    {
//        if (it->port == port && it->host == host)
//        {
//            delete (it->socket);
//            it = forwardedStreams_.erase(it);
//        }
//        else
//            ++it;
//    }
//}

//const std::string& StreamSession::getNickname() const
//{
//    return nickname_;
//}

//const std::string& StreamSession::getSessionID() const
//{
//    return sessionID_;
//}

//const std::string& StreamSession::getMyDestination() const
//{
//    return myDestination_;
//}

//const std::string& StreamSession::getHost() const
//{
//    return socket_->getHost();
//}

//uint16_t StreamSession::getPort() const
//{
//    return socket_->getPort();
//}

//const std::string& StreamSession::getMinVer() const
//{
//    return socket_->getMinVer();
//}

//const std::string& StreamSession::getMaxVer() const
//{
//    return socket_->getMaxVer();
//}

//const std::string& StreamSession::getVersion() const
//{
//    return socket_->getVersion();
//}

//const sockaddr_in& StreamSession::getAddress() const
//{
//    return socket_->getAddress();
//}

//bool StreamSession::isDestinationGenerated() const
//{
//    return isGenerated_;
//}

//--------------------------------------------------------------------------------------------------


std::string Message::createSAMRequest(const char* format, ...)
{
    char buffer[SAM_BUFSIZE];
    memset(buffer, 0, SAM_BUFSIZE);

    va_list args;
    va_start (args, format);
    const int sizeToSend = vsnprintf(buffer, SAM_BUFSIZE, format, args);
    va_end(args);

    if (sizeToSend < 0)
    {
        print_error("Failed to format message");
        return std::string();
    }
    return std::string(buffer);
}

std::string Message::hello(const std::string &minVer, const std::string &maxVer)
{
///////////////////////////////////////////////////////////
//
//    ->  HELLO VERSION
//              MIN=$min
//              MAX=$max
//
//    <-  HELLO REPLY
//              RESULT=OK
//              VERSION=$version
//
///////////////////////////////////////////////////////////

    static const char* helloFormat = "HELLO VERSION MIN=%s MAX=%s\n";
    return createSAMRequest(helloFormat, minVer.c_str(), maxVer.c_str());
}

std::string Message::sessionCreate(SessionStyle style, const std::string& sessionID, const std::string& nickname, const std::string& destination /*= SAM_GENERATE_MY_DESTINATION*/, const std::string& options /*= ""*/)
{
///////////////////////////////////////////////////////////
//
//    ->  SESSION CREATE
//              STYLE={STREAM,DATAGRAM,RAW}
//              ID={$nickname}
//              DESTINATION={$private_destination_key,TRANSIENT}
//              [option=value]*
//
//    <-  SESSION STATUS
//              RESULT=OK
//              DESTINATION=$private_destination_key
//
///////////////////////////////////////////////////////////

    std::string sessionStyle;
    switch(style)
    {
    case sssStream:   sessionStyle = "STREAM";   break;
    case sssDatagram: sessionStyle = "DATAGRAM"; break;
    case sssRaw:      sessionStyle = "RAW";      break;
    }

    static const char* sessionCreateFormat = "SESSION CREATE STYLE=%s ID=%s DESTINATION=%s inbound.nickname=%s %s\n";  // we add inbound.nickname option
    return createSAMRequest(sessionCreateFormat, sessionStyle.c_str(), sessionID.c_str(), destination.c_str(), nickname.c_str(), options.c_str());
}

std::string Message::streamAccept(const std::string& sessionID, bool silent /*= false*/)
{
///////////////////////////////////////////////////////////
//
//    ->  STREAM ACCEPT
//             ID={$nickname}
//             [SILENT={true,false}]
//
//    <-  STREAM STATUS
//             RESULT=$result
//             [MESSAGE=...]
//
///////////////////////////////////////////////////////////

    static const char* streamAcceptFormat = "STREAM ACCEPT ID=%s SILENT=%s\n";
    return createSAMRequest(streamAcceptFormat, sessionID.c_str(), silent ? "true" : "false");
}

std::string Message::streamConnect(const std::string& sessionID, const std::string& destination, bool silent /*= false*/)
{
///////////////////////////////////////////////////////////
//
//    ->  STREAM CONNECT
//             ID={$nickname}
//             DESTINATION=$peer_public_base64_key
//             [SILENT={true,false}]
//
//    <-  STREAM STATUS
//             RESULT=$result
//             [MESSAGE=...]
//
///////////////////////////////////////////////////////////

    static const char* streamConnectFormat = "STREAM CONNECT ID=%s DESTINATION=%s SILENT=%s\n";
    return createSAMRequest(streamConnectFormat, sessionID.c_str(), destination.c_str(), silent ? "true" : "false");
}

std::string Message::streamForward(const std::string& sessionID, const std::string& host, uint16_t port, bool silent /*= false*/)
{
///////////////////////////////////////////////////////////
//
//    ->  STREAM FORWARD
//             ID={$nickname}
//             PORT={$port}
//             [HOST={$host}]
//             [SILENT={true,false}]
//
//    <-  STREAM STATUS
//             RESULT=$result
//             [MESSAGE=...]
//
///////////////////////////////////////////////////////////
    static const char* streamForwardFormat = "STREAM FORWARD ID=%s PORT=%u HOST=%s SILENT=%s\n";
    return createSAMRequest(streamForwardFormat, sessionID.c_str(), (unsigned)port, host.c_str(), silent ? "true" : "false");
}

std::string Message::namingLookup(const std::string& name)
{
///////////////////////////////////////////////////////////
//
//    -> NAMING LOOKUP
//            NAME=$name
//
//    <- NAMING REPLY
//            RESULT=OK
//            NAME=$name
//            VALUE=$base64key
//
///////////////////////////////////////////////////////////

    static const char* namingLookupFormat = "NAMING LOOKUP NAME=%s\n";
    return createSAMRequest(namingLookupFormat, name.c_str());
}

std::string Message::destGenerate()
{
///////////////////////////////////////////////////////////
//
//    -> DEST GENERATE
//
//    <- DEST REPLY
//            PUB=$pubkey
//            PRIV=$privkey
//
///////////////////////////////////////////////////////////

    static const char* destGenerateFormat = "DEST GENERATE\n";
    return createSAMRequest(destGenerateFormat);
}

#define SAM_MAKESTRING(X) SAM_MAKESTRING2(X)
#define SAM_MAKESTRING2(X) #X

#define SAM_CHECK_RESULT(value) \
    if (result == SAM_MAKESTRING(value)) return value

Message::eStatus Message::checkAnswer(const std::string& answer)
{
    if (answer.empty())
        return EMPTY_ANSWER;

    const std::string result = getValue(answer, "RESULT");

    SAM_CHECK_RESULT(OK);
    SAM_CHECK_RESULT(DUPLICATED_DEST);
    SAM_CHECK_RESULT(DUPLICATED_ID);
    SAM_CHECK_RESULT(I2P_ERROR);
    SAM_CHECK_RESULT(INVALID_ID);
    SAM_CHECK_RESULT(INVALID_KEY);
    SAM_CHECK_RESULT(CANT_REACH_PEER);
    SAM_CHECK_RESULT(TIMEOUT);
    SAM_CHECK_RESULT(NOVERSION);
    SAM_CHECK_RESULT(KEY_NOT_FOUND);
    SAM_CHECK_RESULT(PEER_NOT_FOUND);
    SAM_CHECK_RESULT(ALREADY_ACCEPTING);

    return CANNOT_PARSE_ERROR;
}

#undef SAM_CHECK_RESULT
#undef SAM_MAKESTRING2
#undef SAM_MAKESTRING

std::string Message::getValue(const std::string& answer, const std::string& key)
{
    if (key.empty())
        return std::string();

    const std::string keyPattern = key + "=";
    size_t valueStart = answer.find(keyPattern);
    if (valueStart == std::string::npos)
        return std::string();

    valueStart += keyPattern.length();
    size_t valueEnd = answer.find_first_of(' ', valueStart);
    if (valueEnd == std::string::npos)
        valueEnd = answer.find_first_of('\n', valueStart);
    return answer.substr(valueStart, valueEnd - valueStart);
}


} // namespace SAM