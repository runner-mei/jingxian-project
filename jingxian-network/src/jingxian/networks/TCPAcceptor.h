
#ifndef _TCPAcceptor_H_
#define _TCPAcceptor_H_

#include "jingxian/config.h"

#if !defined (JINGXIAN_LACKS_PRAGMA_ONCE)
# pragma once
#endif /* JINGXIAN_LACKS_PRAGMA_ONCE */

// Include files
# include "jingxian/string/string.hpp"
# include "jingxian/IReactorCore.h"
# include "jingxian/networks/TCPEndpoint.h"
# include "jingxian/networks/sockets/base_socket.h"
# include "jingxian/networks/proactor.h"

_jingxian_begin

class TCPAcceptor : public IAcceptor
{
public:
	
	TCPAcceptor(IOCPServer* core, IProtocolFactory* protocolFactory, const tchar* endpoint);
	
	virtual ~TCPAcceptor();

	/**
	 * @implements timeout
	 */
    virtual time_t timeout() const;

	/**
	 * @implements bindPoint
	 */
    virtual const IEndpoint& bindPoint() const;

	/**
	 * @implements isListening
	 */
	virtual bool isListening() const;

	/**
	 * @implements stopListening
	 */
    virtual void stopListening();

	/**
	 * @implements startListening
	 */
    virtual bool startListening();

	/**
	 * @implements protocolFactory
	 */
    virtual IProtocolFactory& protocolFactory();

	/**
	 * accept ����Ļص�
	 */
	void on_complete( const char* ptr
		                        , size_t bytes_transferred
								, int success
								, void *completion_key
								, u_int32_t error);
	/**
	 * @implements misc
	 */
    virtual IDictionary& misc();

	/**
	 * @implements misc
	 */
	virtual const IDictionary& misc() const;

	/**
	* @implements toString
	*/
	virtual const tstring& toString() const;
	
private:
	NOCOPY(TCPAcceptor);

	void initializeConnection(int bytesTransferred
		, void *completion_key);

	void decrementAccepting();

	bool doAccept();

	IOCPServer* server_;
	IProtocolFactory* protocolFactory_;
	base_socket socket_;
	TCPEndpoint endpoint_;
	connection_status::type status_;
	ILogger* logger_;
	tstring toString_;
};


class TCPAcceptorFactory : public IAcceptorFactory
{
public:

	TCPAcceptorFactory(IOCPServer* core);

	virtual ~TCPAcceptorFactory();

	/**
	* @implements createAcceptor
	 */
	virtual IAcceptor* createAcceptor(const tchar* endPoint, IProtocolFactory* protocolFactory);

	/**
	* @implements toString
	 */
	virtual const tstring& toString() const;
	
private:
	NOCOPY(TCPAcceptorFactory);
	
	IOCPServer* server_;
	tstring toString_;	
};

_jingxian_end

#endif //_TCPAcceptor_H_ 