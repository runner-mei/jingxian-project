
#ifndef _base_socket_h_
#define _base_socket_h_

#include "jingxian/config.h"

#if !defined (JINGXIAN_LACKS_PRAGMA_ONCE)
# pragma once
#endif /* JINGXIAN_LACKS_PRAGMA_ONCE */

// Include files
# include "Winsock2.h"
# include "Mswsock.h"
# include "jingxian/networks/NetAddress.h"

typedef WSABUF iovec;

# ifndef ___iopack___
# define ___iopack___
 typedef TRANSMIT_PACKETS_ELEMENT iopack;
# endif // ___iopack___

 # ifndef ___iofile___
# define ___iopack___
 typedef TRANSMIT_FILE_BUFFERS io_file_buf;
# endif // ___iopack___


_jingxian_begin

enum select_mode
{
	  select_read = 1
	, select_write = 2
	, select_error = 4
};

class BaseSocket
{
public:

  ~BaseSocket (void);

  /**
   * 是否创建有效
   */
  bool is_good() const;

  /**
   * 交换双方的内部值
   */
  void swap( BaseSocket& r );

  /**
   * 取得socket对象
   */
  SOCKET handle (void) const;

  /**
   * 设置socket对象
   */
  void set_handle (SOCKET handle);

  /**
   * 设置socket选项，请见setsockopt
   */
  bool set_option (int level,
                  int option,
                  void *optval,
                  int optlen) const;
  /**
   * 获得socket选项，请见getsockopt
   */
  bool get_option (int level,
                  int option,
                  void *optval,
                  int *optlen) const;

  /**
   * 关闭socket
   */
  void close (void);
  
  /**
   * 判断并等待直到socket可以进行读(写)操作，或出错，或超时
   * @params[ in ] timval 超时时间
   * @params[ in ] mode 判断的的操作类型，请见select_mode枚举
   * @return 可以操作返回true
   */
  bool poll( const TIMEVAL& timeval, int select_mode);

  /**
   * 返回一个人可读的字符串
   */
  const tstring& toString() const;

  /**
   * 初始化socket服务
   */
  static bool initializeScket();

  /**
   * 关闭socket服务
   */
  static void shutdownSocket();

  BaseSocket (void);

  BaseSocket ( int protocol_family,
			int type,
            int protocol = 0,
            int reuse_addr = 0);

  BaseSocket ( int protocol_family,
			int type,
            int protocol,
            LPWSAPROTOCOL_INFO protocolinfo,
            GROUP g,
             u_long flags,
            int reuse_addr);

  /**
   * 启动socket的选项
   * @params[ in ] value 可取值请见ioctlsocket
   */
  bool enable (int value);

  /**
   * 启动socket的选项
   * @params[ in ] value 可取值请见ioctlsocket
   */
  bool disable (int value);

  bool open (int protocol_family = AF_INET,
			int type = SOCK_STREAM,
            int protocol = IPPROTO_TCP,
            int reuse_addr = 0 );

  bool open (int protocol_family,
			int type,
            int protocol,
            LPWSAPROTOCOL_INFO protocolinfo,
            GROUP g,
            u_long flags,
            int reuse_addr);

private:
  NOCOPY( BaseSocket );

  SOCKET handle_;
  mutable tstring toString_;

public:
	static LPFN_TRANSMITFILE __transmitfile;
	static LPFN_ACCEPTEX __acceptex;
	static LPFN_TRANSMITPACKETS __transmitpackets;
	static LPFN_CONNECTEX __connectex;
	static LPFN_DISCONNECTEX __disconnectex;
	static LPFN_GETACCEPTEXSOCKADDRS __getacceptexsockaddrs;
};

inline tostream& operator<<( tostream& target, const BaseSocket& s )
{
  if( INVALID_SOCKET == s.handle())
  {
	  target << _T("INVALID_SOCKET" );
  }
  else
  {
	  target << s.handle() ;
  }
  
  return target;
}

#if defined (OS_HAS_INLINED)
#include "jingxian/networks/sockets/BaseSocket.inl"
#endif /* OS_HAS_INLINED */

_jingxian_end

#endif /* _base_socket_h_ */
