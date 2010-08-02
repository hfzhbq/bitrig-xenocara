# $OpenBSD: bsd.xconf.mk,v 1.12 2010/08/02 18:45:02 matthieu Exp $

# Shared libs?
.if ${MACHINE_ARCH} == "m88k" || ${MACHINE} == "vax"
XENOCARA_HAVE_SHARED_LIBS?=no
.else
XENOCARA_HAVE_SHARED_LIBS?=yes
.endif

# Build GL libs and apps?
XENOCARA_BUILD_GL?=${XENOCARA_HAVE_SHARED_LIBS:L}
XENOCARA_BUILD_DRI?=${XENOCARA_HAVE_SHARED_LIBS:L}

# Build XCB
XENOCARA_BUILD_XCB?=${XENOCARA_HAVE_SHARED_LIBS}

# Build pixmain?
.if ${MACHINE_ARCH} == "m68k"
XENOCARA_BUILD_PIXMAN?=no
.else
XENOCARA_BUILD_PIXMAN?=yes
.endif
