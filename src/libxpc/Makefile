# bmake component build for src/libxpc.
#
# This is the xpc component of the libSystem family, shaped like Apple's
# libxpc: it links its /usr/lib/system siblings directly and uses
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
DYLIB	 := ${RELEASE}/libxpc.dylib

# Objects built outside this component and linked into it: the root
# Makefile passes liblaunch's.
EXTRA_OBJS ?=

SDKROOT!=	xcrun --show-sdk-path 2>/dev/null || true
INCLUDES := -I${.CURDIR}/include -I${SDKROOT}/usr/include
DEFINES := -DMACOSX -DDARWIN64 -DDARWIN -DBUILD_DARWIN
CFLAGS	:= -std=c11 -fblocks -g -O0 -Wall -Wextra -Werror \
		-MMD -MP ${INCLUDES} ${DEFINES}
# vproc_swap_integer is exported deliberately, matching Apple: the SDK's
# libxpc.tbd and the host's /usr/lib/system/libxpc.dylib both export it (the
# vproc_* API lives in libxpc, not libSystem).  Apple's libvproc.c implements
# it as a MIG client (vproc_mig_swap_integer) for an interface launchd stopped
# serving when it moved to XPC; the MIG stubs are closed, so the flat
# vproc_mig_* references below resolve at runtime against the host's copy
# (libSystem pulls libxpc in) and fail with nonzero when only our launchd is
# up.  Exporting keeps consumers such as launchctl able to bind the symbol.
#
# The export surface is pinned to export.symbols.txt.  Only genuinely
# internal symbols are hidden (the __xpc_bool_{true,false}_val singletons,
# which are already hidden behind the __xpc_bool_{true,false} XPC_GLOBAL_OBJECT
# faces, and xpc_data_create_take, which no shipped header or consumer uses).
# Everything else that is declared in the shipped xpc/ headers or referenced
# by in-tree consumers (launchd, launchctl, test-connections, launchd_stub)
# stays exported, matching Apple's libxpc symbol set.
EXPORT_LIST := ${.CURDIR}/export.symbols.txt
LDFLAGS	:= -dynamiclib -install_name @rpath/libxpc.dylib \
		-Wl,-exported_symbols_list,${EXPORT_LIST} \
		-L${SDKROOT}/usr/lib/system -L${SDKROOT}/usr/lib \
		-lbsm

# Apple's link graph for libxpc: the siblings that still exist as
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

# Habitat install: /usr/lib/system/libxpc.dylib with Apple's
# install name, plus headers.  DESTDIR for staged/OS builds.
DESTDIR ?=
install: ${DYLIB}
	@mkdir -p ${DESTDIR}/usr/lib/system ${DESTDIR}/usr/local/include
	install -m 0755 ${DYLIB} ${DESTDIR}/usr/lib/system/libxpc.dylib
	install_name_tool -id /usr/lib/system/libxpc.dylib \
	    ${DESTDIR}/usr/lib/system/libxpc.dylib
	install -m 0644 ${.CURDIR}/include/xpc.h ${DESTDIR}/usr/local/include/

clean:
	rm -rf ${OBJDIR} ${RELEASE}

-include ${DEPFILES}
