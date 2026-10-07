# bmake component build for src/libsystem_xpc.
#
# This is the xpc component of the libSystem family, shaped like Apple's
# libsystem_xpc: it links its /usr/lib/system siblings directly and uses
# the same LIBRARY_SEARCH_PATHS line Apple's Libsystem.xcconfig uses.
#
#   LIBRARY_SEARCH_PATHS = $(SDKROOT)/$(SDK_INSTALL_ROOT)/usr/lib/system
#
# On modern Darwin the SDK ships re-export .tbd stubs for the siblings that
# still have separate install names (libsystem_info, libsystem_notify,
# libsystem_trace); the others (log, nv, sbuf) have been merged into
# libSystem and are reached through -lSystem instead.

# Standalone default: build locally.  The root Makefile passes RELEASE and
# OBJDIR explicitly so the whole tree shares one build/ directory.
BUILD	 ?= ${.CURDIR}/build
OBJDIR	 ?= ${BUILD}/obj
RELEASE	 ?= ${BUILD}/release
DYLIB	 := ${RELEASE}/libsystem_xpc.dylib

# Objects built outside this component and linked into it: the root
# Makefile passes liblaunch's.
EXTRA_OBJS ?=

SDKROOT!=	xcrun --show-sdk-path 2>/dev/null || true
INCLUDES := -I${.CURDIR}/include -I${SDKROOT}/usr/include
DEFINES := -DMACOSX -DDARWIN64 -DDARWIN -DBUILD_DARWIN
CFLAGS	:= -std=c11 -fblocks -g -O0 -Wall -Wextra -Werror \
		-MMD -MP ${INCLUDES} ${DEFINES}
# vproc_swap_integer is unexported on purpose.  Apple's libvproc.c implements it
# as a MIG client (vproc_mig_swap_integer) for an interface launchd stopped
# serving when it moved to XPC, so ours always returns nonzero.  macOS's own
# copy -- closed when the vproc_* API moved into libxpc -- still works, and
# lives in libSystem, so leaving this out of the export table is what lets
# launchctl's direct calls reach it instead of the dead one here.  Internal
# callers in liblaunch/libvproc still link against ours; only the export is
# suppressed.
LDFLAGS	:= -dynamiclib -install_name @rpath/libsystem_xpc.dylib \
		-L${SDKROOT}/usr/lib/system -L${SDKROOT}/usr/lib \
		-Wl,-unexported_symbol,_vproc_swap_integer -lbsm

# Apple's link graph for libsystem_xpc: the siblings that still exist as
# separate /usr/lib/system install names on this SDK generation.
LIB_SYSTEM_LIBS := -lsystem_info -lsystem_notify -lsystem_trace

LIB_SRCS!=	find ${.CURDIR}/object ${.CURDIR}/wire ${.CURDIR}/pipe \
		${.CURDIR}/connection -name '*.c' | sort
OBJS	:= ${LIB_SRCS:T:S,.c$,.o,:S,^,${OBJDIR}/,}
DEPFILES:= ${OBJS:S,.o$,.d,}

.PHONY: all install clean

all: ${DYLIB}

${OBJDIR}:
	@mkdir -p $@

${RELEASE}:
	@mkdir -p $@

.for _s in ${LIB_SRCS}
${OBJDIR}/${_s:T:R}.o: ${_s}
	@mkdir -p ${OBJDIR}
	${CC} ${CFLAGS} -c ${_s} -o ${.TARGET}
.endfor

${DYLIB}: ${OBJS} ${EXTRA_OBJS} ${RELEASE}
	${CC} ${LDFLAGS} -o $@ ${OBJS} ${EXTRA_OBJS} ${LIB_SYSTEM_LIBS}

# Habitat install: /usr/lib/system/libsystem_xpc.dylib with Apple's
# install name, plus headers.  DESTDIR for staged/OS builds.
DESTDIR ?=
install: ${DYLIB}
	@mkdir -p ${DESTDIR}/usr/lib/system ${DESTDIR}/usr/local/include
	install -m 0755 ${DYLIB} ${DESTDIR}/usr/lib/system/libsystem_xpc.dylib
	install_name_tool -id /usr/lib/system/libsystem_xpc.dylib \
	    ${DESTDIR}/usr/lib/system/libsystem_xpc.dylib
	install -m 0644 ${.CURDIR}/include/xpc.h ${DESTDIR}/usr/local/include/

clean:
	rm -rf ${OBJDIR} ${RELEASE}

-include ${DEPFILES}
