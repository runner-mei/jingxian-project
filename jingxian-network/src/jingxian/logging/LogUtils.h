
#ifndef _LOGUTILS_H_
#define _LOGUTILS_H_

#include "jingxian/config.h"

#if !defined (JINGXIAN_LACKS_PRAGMA_ONCE)
# pragma once
#endif /* JINGXIAN_LACKS_PRAGMA_ONCE */

// Include files
# include "jingxian/logging/logger.h"
# include "jingxian/logging/tracer.h"
# include "jingxian/jingxian_export.h"


_jingxian_begin

namespace logging
{
	ILogger* makeLogger( const tchar* nm );

	ILogger* makeLogger( const tstring& nm );

	ITracer* makeTracer( const tchar* nm );

	ITracer* makeTracer( const tstring& nm );
}

_jingxian_end

#endif //_LOGUTILS_H_