
/**
 * -*- C++ -*-
 * -------------------------------------------------------------------------------
 * - ��q�Шr �q�Шr                        config.h,v 1.0 2004/12/09 16:01:34
 *  �u�������� �q�q �Шr
 * ���������| �t------
 * -------------------------------------------------------------------------------
 */

#ifndef _config_h_
#define _config_h_


#if !defined (JINGXIAN_LACKS_PRAGMA_ONCE)
# pragma once
#endif /* JINGXIAN_LACKS_PRAGMA_ONCE */

#include "jingxian/pro.h"

#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_   /* Prevent inclusion of winsock.h in windows.h */
#endif // _WINSOCKAPI_

# include <windows.h>
# include <assert.h>
# include <time.h>
# include <memory>

#ifndef _u_char_
#define _u_char_
typedef unsigned char u_char;
#endif // _u_char_


#ifndef _u_int8_t_
#define _u_int8_t_
typedef unsigned char uint8_t;
#endif // _u_int16_t_

#ifndef _u_int16_t_
#define _u_int16_t_
typedef unsigned short uint16_t;
#endif // _u_int16_t_

#ifndef _u_int32_t_
#define _u_int32_t_
typedef unsigned int uint32_t;
#endif // _u_int16_t_

#ifndef _u_int64_t_
#define _u_int64_t_
typedef unsigned __int64 uint64_t;
#endif // _u_int16_t_

#ifndef __GNUG__

#ifndef _ssize_t_
#define _ssize_t_
typedef int ssize_t;
#endif // _ssize_t_

#ifndef _int8_t_
#define _int8_t_
typedef char int8_t;
#endif // _int16_t_

#ifndef _int16_t_
#define _int16_t_
typedef short int16_t;
#endif // _int16_t_

#ifndef _int32_t_
#define _int32_t_
typedef int int32_t;
#endif // _int32_t_

#ifndef _int64_t_
#define _int64_t_
typedef  __int64 int64_t;
#endif // _u_int64_t_

#endif

#ifndef _Byte_
#define _Byte_
typedef char Byte;
#endif // _Byte_

#ifndef _Int16_
#define _Int16_
typedef short Int16;
#endif //_Int16_

#ifndef _Int32_
#define _Int32_
typedef int Int32;
#endif // Int32

#ifndef _Int64_
#define _Int64_
typedef __int64 Int64;
#endif //

#ifndef _Float_
#define _Float_
typedef float Float;
#endif // _Float_

#ifndef _Double_
#define _Double_
typedef double Double;
#endif //

#ifndef _off_t
typedef long _off_t;
#endif //


#ifndef off_t
typedef _off_t off_t;
#endif //

#define _jingxian_begin
#define _jingxian_end
#define _jingxian
#define use_jingxian


#define HAVE_ITERATOR

#define NOCOPY( CLASS ) CLASS( const CLASS & ); CLASS& operator=( const CLASS & )


# ifdef HAVE_ITERATOR
#  define ITERATOR_BASE(cat,val,diff) : public std::iterator<std::cat##_tag,val,diff>
# elif HAVE_ITERATOR_TRAITS
#  define ITERATOR_BASE(cat,val,diff)
# else
#  define ITERATOR_BASE(cat,val,diff) : public std::cat<val,diff>
# endif

# ifdef HAVE_REVERSE_ITERATOR_STYLE
#  define REVERSE_ITERATOR(tname,tag,iter,value,reference,pointer,difference) \
  std::reverse_iterator<iter>
# elif HAVE_REVERSE_ITERATOR_STYLE == 2
#  define REVERSE_ITERATOR(tname,tag,iter,value,reference,pointer,difference) \
  std::reverse_iterator<iter,std::tag,value,reference,pointer,difference>
# elif HAVE_REVERSE_ITERATOR_STYLE == 3
#  define REVERSE_ITERATOR(tname,tag,iter,value,reference,pointer,difference) \
  std::tname<iter,value,reference,pointer,difference>
# elif HAVE_REVERSE_ITERATOR_STYLE == 4
#  define REVERSE_ITERATOR(tname,tag,iter,value,reference,pointer,difference) \
  std::os_##tname<iter,value,reference,pointer,difference>
# else
#  define REVERSE_ITERATOR(tname,tag,iter,value,reference,pointer,difference) \
  I_don_t_know_how_to_define_reverse_iterator
# endif


template< typename T >
inline bool is_null(T t)
{
    return (NULL == t);
}

template< typename T >
inline bool is_null(const std::auto_ptr<T>& t)
{
    return (NULL == t.get());
}

template< typename T >
inline T get_ptr(T t)
{
    return t;
}

#define JINGXIAN_BIT_ENABLED( V , F ) ( V&F )

#define JINGXIAN_UNUSED_ARG( X )

#define MAKE_STRING( X ) #X
#define ASSERT assert



#if defined (OS_HAS_INLINED)
#define OS_INLINE inline
#else
#define OS_INLINE
#endif


#define jingxian_assert assert

#define null_ptr NULL

#ifdef __GNUG__
#define _T L
typedef  int errno_t;
#define _THROW0() throw()
#define __MSVCRT_VERSION__ 0x0700
#endif

typedef DWORD errcode_t;

void* my_calloc(__in size_t _NumOfElements, __in size_t _SizeOfElements);
void  my_free(__inout_opt void * _Memory);
void* my_malloc(__in size_t _Size);                 
void* my_realloc(__in_opt void * _Memory, __in size_t _NewSize);

char*  my_strdup(__in_z_opt const char * _Src);
wchar_t* my_wcsdup(__in_z const wchar_t * _Str);

//#define my_free free
//#define my_malloc malloc
//#define my_calloc calloc
//#define my_realloc realloc
//#define my_strdup _strdup
//#define my_wcsdup _wcsdup

#define JINGXIAN_MT   1

#endif // _config_h_
