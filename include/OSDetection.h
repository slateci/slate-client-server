#ifndef SLATE_OSDETECTION_H
#define SLATE_OSDETECTION_H

//This is extracted on boost::predef from boost 1.62, since CentOS 7 does not
//have a new enough version of boost packaged to include predef. It should go
//away if we can get access to less-ancient boost. 

/*`
[heading `BOOST_VERSION_NUMBER`]

``
BOOST_VERSION_NUMBER(major,minor,patch)
``

Defines standard version numbers, with these properties:

* Decimal base whole numbers in the range \[0,1000000000).
  The number range is designed to allow for a (2,2,5) triplet.
  Which fits within a 32 bit value.
* The `major` number can be in the \[0,99\] range.
* The `minor` number can be in the \[0,99\] range.
* The `patch` number can be in the \[0,99999\] range.
* Values can be specified in any base. As the defined value
  is an constant expression.
* Value can be directly used in both preprocessor and compiler
  expressions for comparison to other similarly defined values.
* The implementation enforces the individual ranges for the
  major, minor, and patch numbers. And values over the ranges
  are truncated (modulo).

*/
#define BOOST_VERSION_NUMBER(major,minor,patch) \
    ( (((major)%100)*10000000) + (((minor)%100)*100000) + ((patch)%100000) )

#define BOOST_VERSION_NUMBER_MAX \
    BOOST_VERSION_NUMBER(99,99,99999)

#define BOOST_VERSION_NUMBER_ZERO \
    BOOST_VERSION_NUMBER(0,0,0)

#define BOOST_VERSION_NUMBER_MIN \
    BOOST_VERSION_NUMBER(0,0,1)

#define BOOST_VERSION_NUMBER_AVAILABLE \
    BOOST_VERSION_NUMBER_MIN

#define BOOST_VERSION_NUMBER_NOT_AVAILABLE \
    BOOST_VERSION_NUMBER_ZERO

/*`
[heading `BOOST_OS_AIX`]

[@http://en.wikipedia.org/wiki/AIX_operating_system IBM AIX] operating system.
Version number available as major, minor, and patch.
 
[table
    [[__predef_symbol__] [__predef_version__]]
    
    [[`_AIX`] [__predef_detection__]]
    [[`__TOS_AIX__`] [__predef_detection__]]
    
    [[`_AIX43`] [4.3.0]]
    [[`_AIX41`] [4.1.0]]
    [[`_AIX32`] [3.2.0]]
    [[`_AIX3`] [3.0.0]]
    ]
*/
 
#define BOOST_OS_AIX BOOST_VERSION_NUMBER_NOT_AVAILABLE
 
#if !defined(BOOST_PREDEF_DETAIL_OS_DETECTED) && ( \
defined(_AIX) || defined(__TOS_AIX__) \
)
#   undef BOOST_OS_AIX
#   if !defined(BOOST_OS_AIX) && defined(_AIX43)
#       define BOOST_OS_AIX BOOST_VERSION_NUMBER(4,3,0)
#   endif
#   if !defined(BOOST_OS_AIX) && defined(_AIX41)
#       define BOOST_OS_AIX BOOST_VERSION_NUMBER(4,1,0)
#   endif
#   if !defined(BOOST_OS_AIX) && defined(_AIX32)
#       define BOOST_OS_AIX BOOST_VERSION_NUMBER(3,2,0)
#   endif
#   if !defined(BOOST_OS_AIX) && defined(_AIX3)
#       define BOOST_OS_AIX BOOST_VERSION_NUMBER(3,0,0)
#   endif
#   if !defined(BOOST_OS_AIX)
#       define BOOST_OS_AIX BOOST_VERSION_NUMBER_AVAILABLE
#   endif
#endif
 
#if BOOST_OS_AIX
#   define BOOST_OS_AIX_AVAILABLE
#   ifndef BOOST_PREDEF_DETAIL_OS_DETECTED
#       define BOOST_PREDEF_DETAIL_OS_DETECTED 1
#   endif
#endif
 
#define BOOST_OS_AIX_NAME "IBM AIX"
 
/*`
[heading `BOOST_OS_AMIGAOS`]

[@http://en.wikipedia.org/wiki/AmigaOS AmigaOS] operating system.

[table
    [[__predef_symbol__] [__predef_version__]]

    [[`AMIGA`] [__predef_detection__]]
    [[`__amigaos__`] [__predef_detection__]]
    ]
 */

#define BOOST_OS_AMIGAOS BOOST_VERSION_NUMBER_NOT_AVAILABLE

#if !defined(BOOST_PREDEF_DETAIL_OS_DETECTED) && ( \
    defined(AMIGA) || defined(__amigaos__) \
    )
#   undef BOOST_OS_AMIGAOS
#   define BOOST_OS_AMIGAOS BOOST_VERSION_NUMBER_AVAILABLE
#endif

#if BOOST_OS_AMIGAOS
#   define BOOST_OS_AMIGAOS_AVAILABLE
#   ifndef BOOST_PREDEF_DETAIL_OS_DETECTED
#       define BOOST_PREDEF_DETAIL_OS_DETECTED 1
#   endif
#endif

#define BOOST_OS_AMIGAOS_NAME "AmigaOS"

/*`
[heading `BOOST_OS_ANDROID`]

[@http://en.wikipedia.org/wiki/Android_%28operating_system%29 Android] operating system.

[table
    [[__predef_symbol__] [__predef_version__]]

    [[`__ANDROID__`] [__predef_detection__]]
    ]
 */

#define BOOST_OS_ANDROID BOOST_VERSION_NUMBER_NOT_AVAILABLE

#if !defined(BOOST_PREDEF_DETAIL_OS_DETECTED) && ( \
    defined(__ANDROID__) \
    )
#   undef BOOST_OS_ANDROID
#   define BOOST_OS_ANDROID BOOST_VERSION_NUMBER_AVAILABLE
#endif

#if BOOST_OS_ANDROID
#   define BOOST_OS_ANDROID_AVAILABLE
#   ifndef BOOST_PREDEF_DETAIL_OS_DETECTED
#       define BOOST_PREDEF_DETAIL_OS_DETECTED 1
#   endif
#endif

#define BOOST_OS_ANDROID_NAME "Android"

/*`
[heading `BOOST_OS_BEOS`]

[@http://en.wikipedia.org/wiki/BeOS BeOS] operating system.

[table
    [[__predef_symbol__] [__predef_version__]]

    [[`__BEOS__`] [__predef_detection__]]
    ]
 */

#define BOOST_OS_BEOS BOOST_VERSION_NUMBER_NOT_AVAILABLE

#if !defined(BOOST_PREDEF_DETAIL_OS_DETECTED) && ( \
    defined(__BEOS__) \
    )
#   undef BOOST_OS_BEOS
#   define BOOST_OS_BEOS BOOST_VERSION_NUMBER_AVAILABLE
#endif

#if BOOST_OS_BEOS
#   define BOOST_OS_BEOS_AVAILABLE
#   ifndef BOOST_PREDEF_DETAIL_OS_DETECTED
#       define BOOST_PREDEF_DETAIL_OS_DETECTED 1
#   endif
#endif

#define BOOST_OS_BEOS_NAME "BeOS"

/* Special case: OSX will define BSD predefs if the sys/param.h
 * header is included. We can guard against that, but only if we
 * detect OSX first. Hence we will force include OSX detection
 * before doing any BSD detection.
 */
/* Special case: iOS will define the same predefs as MacOS, and additionally
 '__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__'. We can guard against that,
 but only if we detect iOS first. Hence we will force include iOS detection
 * before doing any MacOS detection.
 */
/*`
 [heading `BOOST_OS_IOS`]
 
 [@http://en.wikipedia.org/wiki/iOS iOS] operating system.
 
 [table
 [[__predef_symbol__] [__predef_version__]]
 
 [[`__APPLE__`] [__predef_detection__]]
 [[`__MACH__`] [__predef_detection__]]
 [[`__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__`] [__predef_detection__]]
 
 [[`__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__`] [__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__*1000]]
 ]
 */

#define BOOST_OS_IOS BOOST_VERSION_NUMBER_NOT_AVAILABLE

#if !defined(BOOST_PREDEF_DETAIL_OS_DETECTED) && ( \
defined(__APPLE__) && defined(__MACH__) && \
defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__) \
)
#   undef BOOST_OS_IOS
#   define BOOST_OS_IOS (__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__*1000)
#endif

#if BOOST_OS_IOS
#   define BOOST_OS_IOS_AVAILABLE
#   ifndef BOOST_PREDEF_DETAIL_OS_DETECTED
#       define BOOST_PREDEF_DETAIL_OS_DETECTED 1
#   endif
#endif

#define BOOST_OS_IOS_NAME "iOS"

/*`
[heading `BOOST_OS_MACOS`]

[@http://en.wikipedia.org/wiki/Mac_OS Mac OS] operating system.

[table
    [[__predef_symbol__] [__predef_version__]]

    [[`macintosh`] [__predef_detection__]]
    [[`Macintosh`] [__predef_detection__]]
    [[`__APPLE__`] [__predef_detection__]]
    [[`__MACH__`] [__predef_detection__]]

    [[`__APPLE__`, `__MACH__`] [10.0.0]]
    [[ /otherwise/ ] [9.0.0]]
    ]
 */

#define BOOST_OS_MACOS BOOST_VERSION_NUMBER_NOT_AVAILABLE

#if !defined(BOOST_PREDEF_DETAIL_OS_DETECTED) && ( \
    defined(macintosh) || defined(Macintosh) || \
    (defined(__APPLE__) && defined(__MACH__)) \
    )
#   undef BOOST_OS_MACOS
#   if !defined(BOOST_OS_MACOS) && defined(__APPLE__) && defined(__MACH__)
#       define BOOST_OS_MACOS BOOST_VERSION_NUMBER(10,0,0)
#   endif
#   if !defined(BOOST_OS_MACOS)
#       define BOOST_OS_MACOS BOOST_VERSION_NUMBER(9,0,0)
#   endif
#endif

#if BOOST_OS_MACOS
#   define BOOST_OS_MACOS_AVAILABLE
#   ifndef BOOST_PREDEF_DETAIL_OS_DETECTED
#       define BOOST_PREDEF_DETAIL_OS_DETECTED 1
#   endif
#endif

#define BOOST_OS_MACOS_NAME "Mac OS"

/*`
[heading `BOOST_OS_BSD_BSDI`]

[@http://en.wikipedia.org/wiki/BSD/OS BSDi BSD/OS] operating system.

[table
    [[__predef_symbol__] [__predef_version__]]

    [[`__bsdi__`] [__predef_detection__]]
    ]
 */

#define BOOST_OS_BSD_BSDI BOOST_VERSION_NUMBER_NOT_AVAILABLE

#if !defined(BOOST_PREDEF_DETAIL_OS_DETECTED) && ( \
    defined(__bsdi__) \
    )
#   ifndef BOOST_OS_BSD_AVAILABLE
#       define BOOST_OS_BSD BOOST_VERSION_NUMBER_AVAILABLE
#       define BOOST_OS_BSD_AVAILABLE
#   endif
#   undef BOOST_OS_BSD_BSDI
#   define BOOST_OS_BSD_BSDI BOOST_VERSION_NUMBER_AVAILABLE
#endif

#if BOOST_OS_BSD_BSDI
#   define BOOST_OS_BSD_BSDI_AVAILABLE
#   ifndef BOOST_PREDEF_DETAIL_OS_DETECTED
#       define BOOST_PREDEF_DETAIL_OS_DETECTED 1
#   endif
#endif

#define BOOST_OS_BSD_BSDI_NAME "BSDi BSD/OS"

/*`
[heading `BOOST_OS_BSD_DRAGONFLY`]

[@http://en.wikipedia.org/wiki/DragonFly_BSD DragonFly BSD] operating system.

[table
    [[__predef_symbol__] [__predef_version__]]

    [[`__DragonFly__`] [__predef_detection__]]
    ]
 */

#define BOOST_OS_BSD_DRAGONFLY BOOST_VERSION_NUMBER_NOT_AVAILABLE

#if !defined(BOOST_PREDEF_DETAIL_OS_DETECTED) && ( \
    defined(__DragonFly__) \
    )
#   ifndef BOOST_OS_BSD_AVAILABLE
#       define BOOST_OS_BSD BOOST_VERSION_NUMBER_AVAILABLE
#       define BOOST_OS_BSD_AVAILABLE
#   endif
#   undef BOOST_OS_BSD_DRAGONFLY
#   if defined(__DragonFly__)
#       define BOOST_OS_DRAGONFLY_BSD BOOST_VERSION_NUMBER_AVAILABLE
#   endif
#endif

#if BOOST_OS_BSD_DRAGONFLY
#   define BOOST_OS_BSD_DRAGONFLY_AVAILABLE
#   ifndef BOOST_PREDEF_DETAIL_OS_DETECTED
#       define BOOST_PREDEF_DETAIL_OS_DETECTED 1
#   endif
#endif

#define BOOST_OS_BSD_DRAGONFLY_NAME "DragonFly BSD"

/*`
[heading `BOOST_OS_BSD_FREE`]

[@http://en.wikipedia.org/wiki/Freebsd FreeBSD] operating system.

[table
    [[__predef_symbol__] [__predef_version__]]

    [[`__FreeBSD__`] [__predef_detection__]]

    [[`__FreeBSD_version`] [V.R.P]]
    ]
 */

#define BOOST_OS_BSD_FREE BOOST_VERSION_NUMBER_NOT_AVAILABLE

#if !defined(BOOST_PREDEF_DETAIL_OS_DETECTED) && ( \
    defined(__FreeBSD__) \
    )
#   ifndef BOOST_OS_BSD_AVAILABLE
#       define BOOST_OS_BSD BOOST_VERSION_NUMBER_AVAILABLE
#       define BOOST_OS_BSD_AVAILABLE
#   endif
#   undef BOOST_OS_BSD_FREE
#   if defined(__FreeBSD_version)
#       if __FreeBSD_version < 500000
#           define BOOST_OS_BSD_FREE \
                BOOST_PREDEF_MAKE_10_VRP000(__FreeBSD_version)
#       else
#           define BOOST_OS_BSD_FREE \
                BOOST_PREDEF_MAKE_10_VRR000(__FreeBSD_version)
#       endif
#   else
#       define BOOST_OS_BSD_FREE BOOST_VERSION_NUMBER_AVAILABLE
#   endif
#endif

#if BOOST_OS_BSD_FREE
#   define BOOST_OS_BSD_FREE_AVAILABLE
#   ifndef BOOST_PREDEF_DETAIL_OS_DETECTED
#       define BOOST_PREDEF_DETAIL_OS_DETECTED 1
#   endif
#endif

#define BOOST_OS_BSD_FREE_NAME "Free BSD"

/*`
[heading `BOOST_OS_BSD_OPEN`]

[@http://en.wikipedia.org/wiki/Openbsd OpenBSD] operating system.

[table
    [[__predef_symbol__] [__predef_version__]]

    [[`__OpenBSD__`] [__predef_detection__]]

    [[`OpenBSD2_0`] [2.0.0]]
    [[`OpenBSD2_1`] [2.1.0]]
    [[`OpenBSD2_2`] [2.2.0]]
    [[`OpenBSD2_3`] [2.3.0]]
    [[`OpenBSD2_4`] [2.4.0]]
    [[`OpenBSD2_5`] [2.5.0]]
    [[`OpenBSD2_6`] [2.6.0]]
    [[`OpenBSD2_7`] [2.7.0]]
    [[`OpenBSD2_8`] [2.8.0]]
    [[`OpenBSD2_9`] [2.9.0]]
    [[`OpenBSD3_0`] [3.0.0]]
    [[`OpenBSD3_1`] [3.1.0]]
    [[`OpenBSD3_2`] [3.2.0]]
    [[`OpenBSD3_3`] [3.3.0]]
    [[`OpenBSD3_4`] [3.4.0]]
    [[`OpenBSD3_5`] [3.5.0]]
    [[`OpenBSD3_6`] [3.6.0]]
    [[`OpenBSD3_7`] [3.7.0]]
    [[`OpenBSD3_8`] [3.8.0]]
    [[`OpenBSD3_9`] [3.9.0]]
    [[`OpenBSD4_0`] [4.0.0]]
    [[`OpenBSD4_1`] [4.1.0]]
    [[`OpenBSD4_2`] [4.2.0]]
    [[`OpenBSD4_3`] [4.3.0]]
    [[`OpenBSD4_4`] [4.4.0]]
    [[`OpenBSD4_5`] [4.5.0]]
    [[`OpenBSD4_6`] [4.6.0]]
    [[`OpenBSD4_7`] [4.7.0]]
    [[`OpenBSD4_8`] [4.8.0]]
    [[`OpenBSD4_9`] [4.9.0]]
    ]
 */

#define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER_NOT_AVAILABLE

#if !defined(BOOST_PREDEF_DETAIL_OS_DETECTED) && ( \
    defined(__OpenBSD__) \
    )
#   ifndef BOOST_OS_BSD_AVAILABLE
#       define BOOST_OS_BSD BOOST_VERSION_NUMBER_AVAILABLE
#       define BOOST_OS_BSD_AVAILABLE
#   endif
#   undef BOOST_OS_BSD_OPEN
#   if !defined(BOOST_OS_BSD_OPEN) && defined(OpenBSD2_0)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER(2,0,0)
#   endif
#   if !defined(BOOST_OS_BSD_OPEN) && defined(OpenBSD2_1)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER(2,1,0)
#   endif
#   if !defined(BOOST_OS_BSD_OPEN) && defined(OpenBSD2_2)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER(2,2,0)
#   endif
#   if !defined(BOOST_OS_BSD_OPEN) && defined(OpenBSD2_3)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER(2,3,0)
#   endif
#   if !defined(BOOST_OS_BSD_OPEN) && defined(OpenBSD2_4)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER(2,4,0)
#   endif
#   if !defined(BOOST_OS_BSD_OPEN) && defined(OpenBSD2_5)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER(2,5,0)
#   endif
#   if !defined(BOOST_OS_BSD_OPEN) && defined(OpenBSD2_6)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER(2,6,0)
#   endif
#   if !defined(BOOST_OS_BSD_OPEN) && defined(OpenBSD2_7)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER(2,7,0)
#   endif
#   if !defined(BOOST_OS_BSD_OPEN) && defined(OpenBSD2_8)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER(2,8,0)
#   endif
#   if !defined(BOOST_OS_BSD_OPEN) && defined(OpenBSD2_9)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER(2,9,0)
#   endif
#   if !defined(BOOST_OS_BSD_OPEN) && defined(OpenBSD3_0)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER(3,0,0)
#   endif
#   if !defined(BOOST_OS_BSD_OPEN) && defined(OpenBSD3_1)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER(3,1,0)
#   endif
#   if !defined(BOOST_OS_BSD_OPEN) && defined(OpenBSD3_2)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER(3,2,0)
#   endif
#   if !defined(BOOST_OS_BSD_OPEN) && defined(OpenBSD3_3)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER(3,3,0)
#   endif
#   if !defined(BOOST_OS_BSD_OPEN) && defined(OpenBSD3_4)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER(3,4,0)
#   endif
#   if !defined(BOOST_OS_BSD_OPEN) && defined(OpenBSD3_5)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER(3,5,0)
#   endif
#   if !defined(BOOST_OS_BSD_OPEN) && defined(OpenBSD3_6)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER(3,6,0)
#   endif
#   if !defined(BOOST_OS_BSD_OPEN) && defined(OpenBSD3_7)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER(3,7,0)
#   endif
#   if !defined(BOOST_OS_BSD_OPEN) && defined(OpenBSD3_8)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER(3,8,0)
#   endif
#   if !defined(BOOST_OS_BSD_OPEN) && defined(OpenBSD3_9)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER(3,9,0)
#   endif
#   if !defined(BOOST_OS_BSD_OPEN) && defined(OpenBSD4_0)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER(4,0,0)
#   endif
#   if !defined(BOOST_OS_BSD_OPEN) && defined(OpenBSD4_1)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER(4,1,0)
#   endif
#   if !defined(BOOST_OS_BSD_OPEN) && defined(OpenBSD4_2)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER(4,2,0)
#   endif
#   if !defined(BOOST_OS_BSD_OPEN) && defined(OpenBSD4_3)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER(4,3,0)
#   endif
#   if !defined(BOOST_OS_BSD_OPEN) && defined(OpenBSD4_4)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER(4,4,0)
#   endif
#   if !defined(BOOST_OS_BSD_OPEN) && defined(OpenBSD4_5)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER(4,5,0)
#   endif
#   if !defined(BOOST_OS_BSD_OPEN) && defined(OpenBSD4_6)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER(4,6,0)
#   endif
#   if !defined(BOOST_OS_BSD_OPEN) && defined(OpenBSD4_7)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER(4,7,0)
#   endif
#   if !defined(BOOST_OS_BSD_OPEN) && defined(OpenBSD4_8)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER(4,8,0)
#   endif
#   if !defined(BOOST_OS_BSD_OPEN) && defined(OpenBSD4_9)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER(4,9,0)
#   endif
#   if !defined(BOOST_OS_BSD_OPEN)
#       define BOOST_OS_BSD_OPEN BOOST_VERSION_NUMBER_AVAILABLE
#   endif
#endif

#if BOOST_OS_BSD_OPEN
#   define BOOST_OS_BSD_OPEN_AVAILABLE
#   ifndef BOOST_PREDEF_DETAIL_OS_DETECTED
#       define BOOST_PREDEF_DETAIL_OS_DETECTED 1
#   endif
#endif

#define BOOST_OS_BSD_OPEN_NAME "OpenBSD"

/*`
[heading `BOOST_OS_BSD_NET`]

[@http://en.wikipedia.org/wiki/Netbsd NetBSD] operating system.

[table
    [[__predef_symbol__] [__predef_version__]]

    [[`__NETBSD__`] [__predef_detection__]]
    [[`__NetBSD__`] [__predef_detection__]]

    [[`__NETBSD_version`] [V.R.P]]
    [[`NetBSD0_8`] [0.8.0]]
    [[`NetBSD0_9`] [0.9.0]]
    [[`NetBSD1_0`] [1.0.0]]
    [[`__NetBSD_Version`] [V.R.P]]
    ]
 */

#define BOOST_OS_BSD_NET BOOST_VERSION_NUMBER_NOT_AVAILABLE

#if !defined(BOOST_PREDEF_DETAIL_OS_DETECTED) && ( \
    defined(__NETBSD__) || defined(__NetBSD__) \
    )
#   ifndef BOOST_OS_BSD_AVAILABLE
#       define BOOST_OS_BSD BOOST_VERSION_NUMBER_AVAILABLE
#       define BOOST_OS_BSD_AVAILABLE
#   endif
#   undef BOOST_OS_BSD_NET
#   if defined(__NETBSD__)
#       if defined(__NETBSD_version)
#           if __NETBSD_version < 500000
#               define BOOST_OS_BSD_NET \
                    BOOST_PREDEF_MAKE_10_VRP000(__NETBSD_version)
#           else
#               define BOOST_OS_BSD_NET \
                    BOOST_PREDEF_MAKE_10_VRR000(__NETBSD_version)
#           endif
#       else
#           define BOOST_OS_BSD_NET BOOST_VERSION_NUMBER_AVAILABLE
#       endif
#   elif defined(__NetBSD__)
#       if !defined(BOOST_OS_BSD_NET) && defined(NetBSD0_8)
#           define BOOST_OS_BSD_NET BOOST_VERSION_NUMBER(0,8,0)
#       endif
#       if !defined(BOOST_OS_BSD_NET) && defined(NetBSD0_9)
#           define BOOST_OS_BSD_NET BOOST_VERSION_NUMBER(0,9,0)
#       endif
#       if !defined(BOOST_OS_BSD_NET) && defined(NetBSD1_0)
#           define BOOST_OS_BSD_NET BOOST_VERSION_NUMBER(1,0,0)
#       endif
#       if !defined(BOOST_OS_BSD_NET) && defined(__NetBSD_Version)
#           define BOOST_OS_BSD_NET \
                BOOST_PREDEF_MAKE_10_VVRR00PP00(__NetBSD_Version)
#       endif
#       if !defined(BOOST_OS_BSD_NET)
#           define BOOST_OS_BSD_NET BOOST_VERSION_NUMBER_AVAILABLE
#       endif
#   endif
#endif

#if BOOST_OS_BSD_NET
#   define BOOST_OS_BSD_NET_AVAILABLE
#   ifndef BOOST_PREDEF_DETAIL_OS_DETECTED
#       define BOOST_PREDEF_DETAIL_OS_DETECTED 1
#   endif
#endif

#define BOOST_OS_BSD_NET_NAME "DragonFly BSD"

#ifndef BOOST_OS_BSD
#define BOOST_OS_BSD BOOST_VERSION_NUMBER_NOT_AVAILABLE
#endif

#if !defined(BOOST_PREDEF_DETAIL_OS_DETECTED) && ( \
    defined(BSD) || \
    defined(_SYSTYPE_BSD) \
    )
#   undef BOOST_OS_BSD
#   include <sys/param.h>
#   if !defined(BOOST_OS_BSD) && defined(BSD4_4)
#       define BOOST_OS_BSD BOOST_VERSION_NUMBER(4,4,0)
#   endif
#   if !defined(BOOST_OS_BSD) && defined(BSD4_3)
#       define BOOST_OS_BSD BOOST_VERSION_NUMBER(4,3,0)
#   endif
#   if !defined(BOOST_OS_BSD) && defined(BSD4_2)
#       define BOOST_OS_BSD BOOST_VERSION_NUMBER(4,2,0)
#   endif
#   if !defined(BOOST_OS_BSD) && defined(BSD)
#       define BOOST_OS_BSD BOOST_PREDEF_MAKE_10_VVRR(BSD)
#   endif
#   if !defined(BOOST_OS_BSD)
#       define BOOST_OS_BSD BOOST_VERSION_NUMBER_AVAILABLE
#   endif
#endif

#if BOOST_OS_BSD
#   define BOOST_OS_BSD_AVAILABLE
#   ifndef BOOST_PREDEF_DETAIL_OS_DETECTED
#       define BOOST_PREDEF_DETAIL_OS_DETECTED 1
#   endif
#endif

#define BOOST_OS_BSD_NAME "BSD"

/*`
[heading `BOOST_OS_CYGWIN`]

[@http://en.wikipedia.org/wiki/Cygwin Cygwin] evironment.

[table
    [[__predef_symbol__] [__predef_version__]]

    [[`__CYGWIN__`] [__predef_detection__]]
    ]
 */

#define BOOST_OS_CYGWIN BOOST_VERSION_NUMBER_NOT_AVAILABLE

#if !defined(BOOST_PREDEF_DETAIL_OS_DETECTED) && ( \
    defined(__CYGWIN__) \
    )
#   undef BOOST_OS_CYGWIN
#   define BOOST_OS_CYGWIN BOOST_VERSION_NUMBER_AVAILABLE
#endif

#if BOOST_OS_CYGWIN
#   define BOOST_OS_CYGWIN_AVAILABLE
#   ifndef BOOST_PREDEF_DETAIL_OS_DETECTED
#       define BOOST_PREDEF_DETAIL_OS_DETECTED 1
#   endif
#endif

#define BOOST_OS_CYGWIN_NAME "Cygwin"

/*`
[heading `BOOST_OS_HAIKU`]

[@http://en.wikipedia.org/wiki/Haiku_(operating_system) Haiku] operating system.

[table
    [[__predef_symbol__] [__predef_version__]]

    [[`__HAIKU__`] [__predef_detection__]]
    ]
 */

#define BOOST_OS_HAIKU BOOST_VERSION_NUMBER_NOT_AVAILABLE

#if !defined(BOOST_PREDEF_DETAIL_OS_DETECTED) && ( \
    defined(__HAIKU__) \
    )
#   undef BOOST_OS_HAIKU
#   define BOOST_OS_HAIKU BOOST_VERSION_NUMBER_AVAILABLE
#endif

#if BOOST_OS_HAIKU
#   define BOOST_OS_HAIKU_AVAILABLE
#   ifndef BOOST_PREDEF_DETAIL_OS_DETECTED
#       define BOOST_PREDEF_DETAIL_OS_DETECTED 1
#   endif
#endif

#define BOOST_OS_HAIKU_NAME "Haiku"

/*`
[heading `BOOST_OS_HPUX`]

[@http://en.wikipedia.org/wiki/HP-UX HP-UX] operating system.

[table
    [[__predef_symbol__] [__predef_version__]]

    [[`hpux`] [__predef_detection__]]
    [[`_hpux`] [__predef_detection__]]
    [[`__hpux`] [__predef_detection__]]
    ]
 */

#define BOOST_OS_HPUX BOOST_VERSION_NUMBER_NOT_AVAILABLE

#if !defined(BOOST_PREDEF_DETAIL_OS_DETECTED) && ( \
    defined(hpux) || defined(_hpux) || defined(__hpux) \
    )
#   undef BOOST_OS_HPUX
#   define BOOST_OS_HPUX BOOST_VERSION_NUMBER_AVAILABLE
#endif

#if BOOST_OS_HPUX
#   define BOOST_OS_HPUX_AVAILABLE
#   ifndef BOOST_PREDEF_DETAIL_OS_DETECTED
#       define BOOST_PREDEF_DETAIL_OS_DETECTED 1
#   endif
#endif

#define BOOST_OS_HPUX_NAME "HP-UX"

/*`
[heading `BOOST_OS_IRIX`]

[@http://en.wikipedia.org/wiki/Irix IRIX] operating system.

[table
    [[__predef_symbol__] [__predef_version__]]

    [[`sgi`] [__predef_detection__]]
    [[`__sgi`] [__predef_detection__]]
    ]
 */

#define BOOST_OS_IRIX BOOST_VERSION_NUMBER_NOT_AVAILABLE

#if !defined(BOOST_PREDEF_DETAIL_OS_DETECTED) && ( \
    defined(sgi) || defined(__sgi) \
    )
#   undef BOOST_OS_IRIX
#   define BOOST_OS_IRIX BOOST_VERSION_NUMBER_AVAILABLE
#endif

#if BOOST_OS_IRIX
#   define BOOST_OS_IRIX_AVAILABLE
#   ifndef BOOST_PREDEF_DETAIL_OS_DETECTED
#       define BOOST_PREDEF_DETAIL_OS_DETECTED 1
#   endif
#endif

#define BOOST_OS_IRIX_NAME "IRIX"

/*`
[heading `BOOST_OS_LINUX`]

[@http://en.wikipedia.org/wiki/Linux Linux] operating system.

[table
    [[__predef_symbol__] [__predef_version__]]

    [[`linux`] [__predef_detection__]]
    [[`__linux`] [__predef_detection__]]
    ]
 */

#define BOOST_OS_LINUX BOOST_VERSION_NUMBER_NOT_AVAILABLE

#if !defined(BOOST_PREDEF_DETAIL_OS_DETECTED) && ( \
    defined(linux) || defined(__linux) \
    )
#   undef BOOST_OS_LINUX
#   define BOOST_OS_LINUX BOOST_VERSION_NUMBER_AVAILABLE
#endif

#if BOOST_OS_LINUX
#   define BOOST_OS_LINUX_AVAILABLE
#   ifndef BOOST_PREDEF_DETAIL_OS_DETECTED
#       define BOOST_PREDEF_DETAIL_OS_DETECTED 1
#   endif
#endif

#define BOOST_OS_LINUX_NAME "Linux"

/*`
[heading `BOOST_OS_OS400`]

[@http://en.wikipedia.org/wiki/IBM_i IBM OS/400] operating system.

[table
    [[__predef_symbol__] [__predef_version__]]

    [[`__OS400__`] [__predef_detection__]]
    ]
 */

#define BOOST_OS_OS400 BOOST_VERSION_NUMBER_NOT_AVAILABLE

#if !defined(BOOST_PREDEF_DETAIL_OS_DETECTED) && ( \
    defined(__OS400__) \
    )
#   undef BOOST_OS_OS400
#   define BOOST_OS_OS400 BOOST_VERSION_NUMBER_AVAILABLE
#endif

#if BOOST_OS_OS400
#   define BOOST_OS_OS400_AVAILABLE
#   ifndef BOOST_PREDEF_DETAIL_OS_DETECTED
#       define BOOST_PREDEF_DETAIL_OS_DETECTED 1
#   endif
#endif

#define BOOST_OS_OS400_NAME "IBM OS/400"

/*`
[heading `BOOST_OS_QNX`]

[@http://en.wikipedia.org/wiki/QNX QNX] operating system.
Version number available as major, and minor if possible. And
version 4 is specifically detected.

[table
    [[__predef_symbol__] [__predef_version__]]

    [[`__QNX__`] [__predef_detection__]]
    [[`__QNXNTO__`] [__predef_detection__]]

    [[`_NTO_VERSION`] [V.R.0]]
    [[`__QNX__`] [4.0.0]]
    ]
 */

#define BOOST_OS_QNX BOOST_VERSION_NUMBER_NOT_AVAILABLE

#if !defined(BOOST_PREDEF_DETAIL_OS_DETECTED) && ( \
    defined(__QNX__) || defined(__QNXNTO__) \
    )
#   undef BOOST_OS_QNX
#   if !defined(BOOST_OS_QNX) && defined(_NTO_VERSION)
#       define BOOST_OS_QNX BOOST_PREDEF_MAKE_10_VVRR(_NTO_VERSION)
#   endif
#   if !defined(BOOST_OS_QNX) && defined(__QNX__)
#       define BOOST_OS_QNX BOOST_VERSION_NUMBER(4,0,0)
#   endif
#   if !defined(BOOST_OS_QNX)
#       define BOOST_OS_QNX BOOST_VERSION_NUMBER_AVAILABLE
#   endif
#endif

#if BOOST_OS_QNX
#   define BOOST_OS_QNX_AVAILABLE
#   ifndef BOOST_PREDEF_DETAIL_OS_DETECTED
#       define BOOST_PREDEF_DETAIL_OS_DETECTED 1
#   endif
#endif

#define BOOST_OS_QNX_NAME "QNX"

/*`
[heading `BOOST_OS_SOLARIS`]

[@http://en.wikipedia.org/wiki/Solaris_Operating_Environment Solaris] operating system.

[table
    [[__predef_symbol__] [__predef_version__]]

    [[`sun`] [__predef_detection__]]
    [[`__sun`] [__predef_detection__]]
    ]
 */

#define BOOST_OS_SOLARIS BOOST_VERSION_NUMBER_NOT_AVAILABLE

#if !defined(BOOST_PREDEF_DETAIL_OS_DETECTED) && ( \
    defined(sun) || defined(__sun) \
    )
#   undef BOOST_OS_SOLARIS
#   define BOOST_OS_SOLARIS BOOST_VERSION_NUMBER_AVAILABLE
#endif

#if BOOST_OS_SOLARIS
#   define BOOST_OS_SOLARIS_AVAILABLE
#   ifndef BOOST_PREDEF_DETAIL_OS_DETECTED
#       define BOOST_PREDEF_DETAIL_OS_DETECTED 1
#   endif
#endif

#define BOOST_OS_SOLARIS_NAME "Solaris"

/*`
[heading `BOOST_OS_UNIX`]

[@http://en.wikipedia.org/wiki/Unix Unix Environment] operating system.

[table
    [[__predef_symbol__] [__predef_version__]]

    [[`unix`] [__predef_detection__]]
    [[`__unix`] [__predef_detection__]]
    [[`_XOPEN_SOURCE`] [__predef_detection__]]
    [[`_POSIX_SOURCE`] [__predef_detection__]]
    ]
 */

#define BOOST_OS_UNIX BOOST_VERSION_NUMBER_NOT_AVAILABLE

#if defined(unix) || defined(__unix) || \
    defined(_XOPEN_SOURCE) || defined(_POSIX_SOURCE)
#   undef BOOST_OS_UNIX
#   define BOOST_OS_UNIX BOOST_VERSION_NUMBER_AVAILABLE
#endif

#if BOOST_OS_UNIX
#   define BOOST_OS_UNIX_AVAILABLE
#endif

#define BOOST_OS_UNIX_NAME "Unix Environment"

/*`
[heading `BOOST_OS_SVR4`]

[@http://en.wikipedia.org/wiki/UNIX_System_V SVR4 Environment] operating system.

[table
    [[__predef_symbol__] [__predef_version__]]

    [[`__sysv__`] [__predef_detection__]]
    [[`__SVR4`] [__predef_detection__]]
    [[`__svr4__`] [__predef_detection__]]
    [[`_SYSTYPE_SVR4`] [__predef_detection__]]
    ]
 */

#define BOOST_OS_SVR4 BOOST_VERSION_NUMBER_NOT_AVAILABLE

#if defined(__sysv__) || defined(__SVR4) || \
    defined(__svr4__) || defined(_SYSTYPE_SVR4)
#   undef BOOST_OS_SVR4
#   define BOOST_OS_SVR4 BOOST_VERSION_NUMBER_AVAILABLE
#endif

#if BOOST_OS_SVR4
#   define BOOST_OS_SVR4_AVAILABLE
#endif

#define BOOST_OS_SVR4_NAME "SVR4 Environment"

/*`
[heading `BOOST_OS_VMS`]

[@http://en.wikipedia.org/wiki/Vms VMS] operating system.

[table
    [[__predef_symbol__] [__predef_version__]]

    [[`VMS`] [__predef_detection__]]
    [[`__VMS`] [__predef_detection__]]

    [[`__VMS_VER`] [V.R.P]]
    ]
 */

#define BOOST_OS_VMS BOOST_VERSION_NUMBER_NOT_AVAILABLE

#if !defined(BOOST_PREDEF_DETAIL_OS_DETECTED) && ( \
    defined(VMS) || defined(__VMS) \
    )
#   undef BOOST_OS_VMS
#   if defined(__VMS_VER)
#       define BOOST_OS_VMS BOOST_PREDEF_MAKE_10_VVRR00PP00(__VMS_VER)
#   else
#       define BOOST_OS_VMS BOOST_VERSION_NUMBER_AVAILABLE
#   endif
#endif

#if BOOST_OS_VMS
#   define BOOST_OS_VMS_AVAILABLE
#   ifndef BOOST_PREDEF_DETAIL_OS_DETECTED
#       define BOOST_PREDEF_DETAIL_OS_DETECTED 1
#   endif
#endif

#define BOOST_OS_VMS_NAME "VMS"

/*`
[heading `BOOST_OS_WINDOWS`]

[@http://en.wikipedia.org/wiki/Category:Microsoft_Windows Microsoft Windows] operating system.

[table
    [[__predef_symbol__] [__predef_version__]]

    [[`_WIN32`] [__predef_detection__]]
    [[`_WIN64`] [__predef_detection__]]
    [[`__WIN32__`] [__predef_detection__]]
    [[`__TOS_WIN__`] [__predef_detection__]]
    [[`__WINDOWS__`] [__predef_detection__]]
    ]
 */

#define BOOST_OS_WINDOWS BOOST_VERSION_NUMBER_NOT_AVAILABLE

#if !defined(BOOST_PREDEF_DETAIL_OS_DETECTED) && ( \
    defined(_WIN32) || defined(_WIN64) || \
    defined(__WIN32__) || defined(__TOS_WIN__) || \
    defined(__WINDOWS__) \
    )
#   undef BOOST_OS_WINDOWS
#   define BOOST_OS_WINDOWS BOOST_VERSION_NUMBER_AVAILABLE
#endif

#if BOOST_OS_WINDOWS
#   define BOOST_OS_WINDOWS_AVAILABLE
#   ifndef BOOST_PREDEF_DETAIL_OS_DETECTED
#       define BOOST_PREDEF_DETAIL_OS_DETECTED 1
#   endif
#endif

#define BOOST_OS_WINDOWS_NAME "Microsoft Windows"

#endif //SLATE_OSDETECTION_H
