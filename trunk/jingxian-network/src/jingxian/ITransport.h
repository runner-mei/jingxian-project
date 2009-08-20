
#ifndef _itransport_h_
#define _itransport_h_

#include "jingxian/config.h"

#if !defined (JINGXIAN_LACKS_PRAGMA_ONCE)
# pragma once
#endif /* JINGXIAN_LACKS_PRAGMA_ONCE */

// Include files
# include "exception.hpp"
# include "buffer/IBuffer.h"

_jingxian_begin

class IProtocol;

class ITransport
{
public:
	    virtual ~ITransport(){}

		/**
		 * 初始化 Transport 实例
		 */
		virtual void initialize() = 0;

		/**
         * 指定用 @see{protocol} 接口来接收读到的数据
         */
        virtual void bindProtocol(IProtocol* protocol) = 0;

        /**
         * 开始读数据
         */
        virtual void startReading() = 0;

        /**
         * 停止读数据
         */
        virtual void stopReading() = 0;

        /**
         * 发送数据（注意它是异步的  )
         * @param[ in ] buffer 待发送的数据块
         * @param[ in ] length 数据长度
         */
        virtual void write(buffer_chain_t* buffer) = 0;

        /**
         * 关闭连接
         */
        virtual void disconnection() = 0;
		
		/**
         * 关闭连接
         */
        virtual void disconnection(const tstring& error) = 0;

        /**
         * 源地址
         */
        virtual const tstring& host() const = 0;

        /**
         * 目标地址
         */
        virtual const tstring& peer() const = 0;

        /**
         * 引发 @see{protocol} 的onTimeout事件的超时时间
         */
        virtual time_t timeout() const = 0;

		/**
		 * 取得地址的描述
		 */
		virtual const tstring& toString() const = 0;
};


class ErrorCode
{
public:
	ErrorCode(const tchar* err)
		: isSuccess_(false)
		, code_( 0 )
		, err_( err )
	{}

	ErrorCode(bool success, int code)
		: isSuccess_(success)
		, code_(code)
		, err_(lastError(code))
	{
	}

	ErrorCode(bool success, int code, const tstring& err)
		: isSuccess_(success)
		, code_(code)
		, err_(err)
	{
	}

	ErrorCode(bool success, int code, const tchar* err)
		: isSuccess_(success)
		, code_(code)
		, err_(err)
	{
	}

	virtual ~ErrorCode() { }

	bool isSuccess() const { return isSuccess_; }
	int getCode() const { return code_; }
	const tstring& toString() const { return err_; }

private:
	bool isSuccess_;
	int code_;
	tstring err_;
};

inline tostream& operator<<( tostream& target, const ITransport& transport )
{
	target << transport.toString();
	return target;
}

inline tostream& operator<<( tostream& target, const ErrorCode& err )
{
	target << err.toString();
	return target;
}

typedef void (*OnBuildConnectionComplete)( ITransport* transport, void* context);
typedef void (*OnBuildConnectionError)( const ErrorCode& err,  void* context);

_jingxian_end

#endif //_itransport_h_