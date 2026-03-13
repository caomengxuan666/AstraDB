
#ifndef LIBGOSSIP_EXPORT_H
#define LIBGOSSIP_EXPORT_H

#ifdef LIBGOSSIP_STATIC_DEFINE
#  define LIBGOSSIP_EXPORT
#  define LIBGOSSIP_NO_EXPORT
#else
#  ifndef LIBGOSSIP_EXPORT
#    ifdef libgossip_EXPORTS
        /* We are building this library */
#      define LIBGOSSIP_EXPORT 
#    else
        /* We are using this library */
#      define LIBGOSSIP_EXPORT 
#    endif
#  endif

#  ifndef LIBGOSSIP_NO_EXPORT
#    define LIBGOSSIP_NO_EXPORT 
#  endif
#endif

#ifndef LIBGOSSIP_DEPRECATED
#  define LIBGOSSIP_DEPRECATED __attribute__ ((__deprecated__))
#endif

#ifndef LIBGOSSIP_DEPRECATED_EXPORT
#  define LIBGOSSIP_DEPRECATED_EXPORT LIBGOSSIP_EXPORT LIBGOSSIP_DEPRECATED
#endif

#ifndef LIBGOSSIP_DEPRECATED_NO_EXPORT
#  define LIBGOSSIP_DEPRECATED_NO_EXPORT LIBGOSSIP_NO_EXPORT LIBGOSSIP_DEPRECATED
#endif

/* NOLINTNEXTLINE(readability-avoid-unconditional-preprocessor-if) */
#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef LIBGOSSIP_NO_DEPRECATED
#    define LIBGOSSIP_NO_DEPRECATED
#  endif
#endif

#endif /* LIBGOSSIP_EXPORT_H */
