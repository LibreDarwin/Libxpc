# bmake (BSD make).  Top-level build for the xnuports Darwin userland.
#
# The tree is shaped like Apple's libSystem family:
#
#   src/libxpc/       the xpc component -> libxpc.dylib (own Makefile)
#   include/         SPI declarations no SDK ships, for the Apple sources
#   mk/patches/      numbered patch series applied to copies of those sources
#   src/launchctl/   clean-room launchctl, kept as an e2e test client only
#   src/launchd/     launchd_stub test double, plus the XPC.framework umbrella
#                     payload (module.modulemap, Info.plist)
#
# launchctl and launchd are Apple's own sources: launchd comes from the
# launchd tree, and launchctl is that same tree's support/launchctl.c (patched
# by 0001/0002).  Neither is reimplemented here.
#
# The apple-oss sources themselves are not vendored here; they live in the
# surrounding CoreOS tree (DarwinSrc/CoreOS/Sources/launchd, .../libinfo)
# and are located by LAUNCHD_UPSTREAM / LIBINFO_UPSTREAM below, so this
# repo can be built either in place or beside its own copies.

CC	?= clang
RM	= rm -rf
ECHO	= echo

BUILD	 := ${.CURDIR}/build
OBJDIR	 := ${BUILD}/obj
RELEASE	 := ${BUILD}/release
TESTDIR	 := ${BUILD}/test
LIBS	 := ${RELEASE}/libxpc.dylib
# launchctl and launchd are Apple's own sources; the stub and the clean-room
# launchctl are test-only and stay out of the release tree.
LAUNCHCTL:= ${RELEASE}/launchctl
LAUNCHD	 := ${TESTDIR}/launchd_stub
TESTCTL	:= ${TESTDIR}/launchctl
FRAMEWORK:= ${RELEASE}/XPC.framework

SDK_PATH!=	xcrun --show-sdk-path 2>/dev/null || true
INCLUDES := -I${.CURDIR}/src/libxpc/include -I${SDK_PATH}/usr/include
DEFINES := -DMACOSX -DDARWIN64 -DDARWIN -DBUILD_DARWIN
CFLAGS	:= -std=c11 -fblocks -g -O0 -Wall -Wextra -Werror \
		-MMD -MP ${INCLUDES} ${DEFINES}

# launchd is Apple's, and stays pristine: the build rsyncs it into
# build/launchd-src and applies mk/patches/launchd/*.patch in order to
# the copy, the way xcode-tools does it: a numbered patch series per
# component, applied with `patch -p1 --forward`.
#
# Upstream lives in the CoreOS tree beside this one.  Candidates are
# probed in order and the first that looks like launchd wins, so the tree
# builds in place and also standalone beside a copy of its own; set
# LAUNCHD_UPSTREAM=<path> to name one explicitly.
.if !defined(LAUNCHD_UPSTREAM)
.for _d in ${.CURDIR}/../../Sources/launchd ${.CURDIR}/src/apple/launchd
.if !defined(LAUNCHD_UPSTREAM) && exists(${_d}/liblaunch/liblaunch.c)
LAUNCHD_UPSTREAM:=	${_d}
.endif
.endfor
.endif

LAUNCHD_SRC	:= ${BUILD}/launchd-src
LAUNCHD_GEN	:= ${BUILD}/gen/launchd
LAUNCHD_PATCHES!=	ls ${.CURDIR}/mk/patches/launchd/*.patch 2>/dev/null || true

${LAUNCHD_SRC}/.patched: ${LAUNCHD_PATCHES}
	@test -n "${LAUNCHD_UPSTREAM}" || { \
	    ${ECHO} "launchd: no upstream launchd found -- tried:"; \
	    ${ECHO} "    ${.CURDIR}/../../Sources/launchd"; \
	    ${ECHO} "    ${.CURDIR}/src/apple/launchd"; \
	    ${ECHO} "pass LAUNCHD_UPSTREAM=<path>"; \
	    exit 1; }
	@mkdir -p ${LAUNCHD_SRC}
	@rsync -a --delete --exclude .git ${LAUNCHD_UPSTREAM}/ ${LAUNCHD_SRC}/
	@for p in ${LAUNCHD_PATCHES}; do \
	    ${ECHO} "  apply $${p}"; \
	    (cd ${LAUNCHD_SRC} && patch -s -p1 --forward < "$$p") || exit 1; \
	done
	@touch $@

patch-apple: ${LAUNCHD_SRC}/.patched

# liblaunch: launchd's liblaunch, libvproc and libbootstrap, built from the
# patched copy into libxpc -- on modern Darwin the launch_*, vproc_*
# and bootstrap_* API lives in libxpc.  They are Apple's sources, so they
# build with Apple's flags (liblaunch.xcconfig) rather than our -Werror.
#
# Their private headers come from xcode-tools' internal SDK, searched after
# the public SDK so it only fills gaps; include/ comes first, for the SPI no
# SDK carries.  A built xcode-tools is found beside this tree, or inside
# LibreDarwin; INTERNAL_SDK=<path> names another.
.if !defined(INTERNAL_SDK)
.for _xct in ${.CURDIR}/../../../../xcode-tools ${.CURDIR}/../xcode-tools \
    ${.CURDIR}/../../../Developer/xcode-tools
_isdk:=	${_xct}/build/release/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.Internal.sdk
.if !defined(INTERNAL_SDK) && exists(${_isdk}/usr/include)
INTERNAL_SDK:=	${_isdk}
.endif
.endfor
.endif

LIBLAUNCH_SRCS	:= ${LAUNCHD_SRC}/liblaunch/liblaunch.c \
		   ${LAUNCHD_SRC}/liblaunch/libvproc.c \
		   ${LAUNCHD_SRC}/liblaunch/libbootstrap.c \
		   ${LAUNCHD_GEN}/jobUser.c \
		   ${LAUNCHD_GEN}/helperUser.c \
		   ${LAUNCHD_GEN}/helperServer.c
LIBLAUNCH_OBJS	:= ${LIBLAUNCH_SRCS:T:R:S,^,${OBJDIR}/liblaunch/,:S,$,.o,}
LIBLAUNCH_CFLAGS:= -isysroot ${SDK_PATH} -fblocks -g -O0 -fvisibility=hidden \
		   -I${LAUNCHD_GEN} -I${.CURDIR}/include \
		   -I${LAUNCHD_SRC}/src -I${LAUNCHD_SRC}/liblaunch \
		   -idirafter ${INTERNAL_SDK}/usr/include \
		   -idirafter ${INTERNAL_SDK}/usr/local/include \
		   -D__MigTypeCheck=1 -Dmig_external=__private_extern__ \
		   -D_DARWIN_USE_64_BIT_INODE=1 -D__DARWIN_NON_CANCELABLE=1 \
		   -DXPC_BUILDING_LAUNCHD=1

# Every MIG interface liblaunch and launchd use: launchd's own, and the
# SDK's mach_exc and notify, whose servers launchd runs.
LAUNCHD_DEFS	:= ${LAUNCHD_SRC}/src/job.defs ${LAUNCHD_SRC}/src/job_reply.defs \
		   ${LAUNCHD_SRC}/src/job_forward.defs \
		   ${LAUNCHD_SRC}/src/internal.defs ${LAUNCHD_SRC}/src/helper.defs \
		   ${SDK_PATH}/usr/include/mach/mach_exc.defs \
		   ${SDK_PATH}/usr/include/mach/notify.defs

${LAUNCHD_GEN}/.mig: ${LAUNCHD_SRC}/.patched
	@test -d "${INTERNAL_SDK}/usr/include" || { \
	    ${ECHO} "liblaunch: no internal SDK -- build xcode-tools, or pass INTERNAL_SDK=<path>"; \
	    exit 1; }
	@mkdir -p ${LAUNCHD_GEN}
.for _d in ${LAUNCHD_DEFS}
	cd ${LAUNCHD_GEN} && mig -isysroot ${SDK_PATH} -DXPC_BUILDING_LAUNCHD=1 \
	    -I${LAUNCHD_SRC}/src -I${LAUNCHD_SRC}/liblaunch \
	    -user ${_d:T:R}User.c -header ${_d:T:R}.h \
	    -server ${_d:T:R}Server.c -sheader ${_d:T:R}Server.h ${_d}
.endfor
	@touch $@

.for _s in ${LIBLAUNCH_SRCS}
${OBJDIR}/liblaunch/${_s:T:R}.o: ${LAUNCHD_GEN}/.mig
	@mkdir -p ${.TARGET:H}
	${CC} ${LIBLAUNCH_CFLAGS} -c ${_s} -o ${.TARGET}
.endfor

# launchd: Apple's launchd-842 from the patched copy, the MIG stubs it
# serves and calls, and the libxpc SPI only it uses (src/launchd/
# xpc_launchd.c), linked against libxpc.  Same
# flags as liblaunch, plus Libinfo's private libinfo.h.  Libinfo is
# apple-oss as well, so it is located the same way launchd is.
.if !defined(LIBINFO_UPSTREAM)
.for _d in ${.CURDIR}/../../Sources/libinfo ${.CURDIR}/src/apple/libinfo
.if !defined(LIBINFO_UPSTREAM) && exists(${_d}/lookup.subproj)
LIBINFO_UPSTREAM:=	${_d}
.endif
.endfor
.endif

# Apple's launchctl pulls in bsm/auditd_lib.h and calls audit_quick_start().
# OpenBSM is apple-oss, so it is located the same way launchd and Libinfo are;
# it is linked into launchctl rather than shipped as a separate library.
.if !defined(OPENBSM_UPSTREAM)
.for _d in ${.CURDIR}/../../Sources/OpenBSM ${.CURDIR}/src/apple/OpenBSM
.if !defined(OPENBSM_UPSTREAM) && exists(${_d}/openbsm/libbsm/bsm_io.c)
OPENBSM_UPSTREAM:=	${_d}/openbsm
.endif
.endfor
.endif

# IOKitUser carries bootfiles.h (kext.subproj), which launchctl includes for
# the boot-* and safe-boot paths.
IOKITUSER_UPSTREAM?= ${.CURDIR}/../../Sources/IOKitUser

LAUNCHD_REAL	:= ${RELEASE}/launchd
LAUNCHD_SRCS	:= ${LAUNCHD_SRC}/src/core.c ${LAUNCHD_SRC}/src/ipc.c \
		   ${LAUNCHD_SRC}/src/kill2.c ${LAUNCHD_SRC}/src/ktrace.c \
		   ${LAUNCHD_SRC}/src/launchd.c ${LAUNCHD_SRC}/src/log.c \
		   ${LAUNCHD_SRC}/src/runtime.c \
		   ${.CURDIR}/src/launchd/xpc_launchd.c \
		   ${LAUNCHD_GEN}/jobServer.c ${LAUNCHD_GEN}/jobUser.c \
		   ${LAUNCHD_GEN}/job_replyUser.c ${LAUNCHD_GEN}/job_forwardUser.c \
		   ${LAUNCHD_GEN}/internalServer.c ${LAUNCHD_GEN}/internalUser.c \
		   ${LAUNCHD_GEN}/helperUser.c \
		   ${LAUNCHD_GEN}/mach_excServer.c ${LAUNCHD_GEN}/notifyServer.c
LAUNCHD_OBJS	:= ${LAUNCHD_SRCS:T:R:S,^,${OBJDIR}/launchd/,:S,$,.o,}
LAUNCHD_CFLAGS	:= ${LIBLAUNCH_CFLAGS} \
		   -idirafter ${LIBINFO_UPSTREAM:UNDEFINED=${.CURDIR}/../../Sources/libinfo}/lookup.subproj

.for _s in ${LAUNCHD_SRCS}
${OBJDIR}/launchd/${_s:T:R}.o: ${LAUNCHD_GEN}/.mig
	@mkdir -p ${.TARGET:H}
	${CC} ${LAUNCHD_CFLAGS} -c ${_s} -o ${.TARGET}
.endfor

${LAUNCHD_REAL}: ${LAUNCHD_OBJS} ${LIBS}
	${CC} -isysroot ${SDK_PATH} ${LAUNCHD_OBJS} -L${RELEASE} -lxpc \
	    -lbsm -Wl,-rpath,${RELEASE} -o $@

.PHONY: all libxpc launchctl launchd test release patch-apple clean ${LIBS}

all: libxpc launchctl launchd release

libxpc: ${LIBS}

launchctl: ${LAUNCHCTL}

launchd: ${LAUNCHD} ${LAUNCHD_REAL}

# The component Makefile owns the object dependency graph (including its
# .d files), so the root always delegates; the sub-make decides freshness.
${LIBS}: ${LIBLAUNCH_OBJS}
	${.MAKE} -C src/libxpc RELEASE=${RELEASE} OBJDIR=${OBJDIR} \
	    EXTRA_OBJS="${LIBLAUNCH_OBJS}"

${RELEASE}:
	@mkdir -p $@

# Apple's launchctl: support/launchctl.c from the patched launchd tree.
#
# Three things it wants that no SDK provides, all supplied here:
#   CoreFoundation/CFPriv.h   our internal SDK ships only public CF headers, so
#                             CF-Root's private ones are symlinked into a shim.
#   systemstats/systemstats.h  closed-source framework; mk/shims declares the
#                             single entry point launchctl calls.
#   SO_EXECPATH                lives in sys/socket_private.h, which nothing
#                             includes, so that header is force-included.
#
# audit_quick_start() and its libbsm backing come from OpenBSM, apple-oss.
APPLE_LAUNCHCTL:= ${LAUNCHD_SRC}/support/launchctl.c
SHIMDIR		:= ${BUILD}/include-shim
SHIMS		:= ${.CURDIR}/mk/shims
CF_PRIVHEADERS	:= ${.CURDIR}/../../Sources/CF-Root/CoreFoundation.framework/Versions/A/PrivateHeaders
BSM_SRCS!=	ls ${OPENBSM_UPSTREAM}/libbsm/*.c 2>/dev/null || true
BSM_OBJS	:= ${BSM_SRCS:T:R:S,^,${OBJDIR}/bsm/,:S,$,.o,}
# "launchctl version" reports the bootstrapper this build came from.
# /bin/launchctl asks launchd for this at run time, but that banner is
# built from code Apple does not publish -- it is in neither its launchd
# tree nor any SDK -- so it is compiled in instead.  For a single-tree
# build libxpc and launchd come from one commit, so the answer is the same
# launchd you are talking to.  Kept in Apple's field order so anything
# parsing the banner still works.
BOOT_VERSION!=	plutil -extract CFBundleShortVersionString raw -o - \
		    ${.CURDIR}/src/libxpc/Info.plist 2>/dev/null || echo 0.0.0
BOOT_DESC!=	git -C ${.CURDIR} describe --always --dirty 2>/dev/null || echo unknown
BOOT_DATE!=	date "+%a %b %e %H:%M:%S %Z %Y"
BSM_CFLAGS	:= -isysroot ${SDK_PATH} -fblocks -g -O0 \
		   -I${OPENBSM_UPSTREAM} -I${OPENBSM_UPSTREAM}/libbsm \
		   -I${OPENBSM_UPSTREAM}/libauditd \
		   -idirafter ${INTERNAL_SDK}/usr/include
LAUNCHCTL_CFLAGS:= -std=gnu11 -fblocks -g -O0 -fvisibility=hidden \
		   -isysroot ${SDK_PATH} -include sys/socket_private.h \
		   -I${.CURDIR}/src/libxpc/include \
		   -I${.CURDIR}/include \
		   -I${LAUNCHD_SRC}/src -I${LAUNCHD_SRC}/liblaunch \
		   -idirafter ${SHIMDIR} \
		   -idirafter ${IOKITUSER_UPSTREAM}/kext.subproj \
		   -idirafter ${OPENBSM_UPSTREAM} \
		   -idirafter ${INTERNAL_SDK}/usr/include \
		   -idirafter ${INTERNAL_SDK}/usr/local/include \
		   -idirafter ${LIBINFO_UPSTREAM:UNDEFINED=${.CURDIR}/../../Sources/libinfo}/lookup.subproj \
		   -D__MigTypeCheck=1 -Dmig_external=__private_extern__ \
		   -D_DARWIN_USE_64_BIT_INODE=1 -D__DARWIN_NON_CANCELABLE=1 \
		   -DXPC_BUILDING_LAUNCHD=1

# CoreFoundation's private headers, under the framework-style path launchctl
# includes them by.  Generated, not tracked: it points outside this repo.
${SHIMDIR}/CoreFoundation:
	@test -d "${CF_PRIVHEADERS}" || { \
	    ${ECHO} "launchctl: no CoreFoundation private headers at ${CF_PRIVHEADERS}"; \
	    exit 1; }
	@mkdir -p ${SHIMDIR}
	@ln -sfn ${CF_PRIVHEADERS} ${SHIMDIR}/CoreFoundation

${SHIMDIR}/xpc_build_version.h:
	@mkdir -p ${SHIMDIR}
	@printf '%s\n' \
	    '/* generated by the build -- do not edit */' \
	    '#define XPC_BOOTSTRAP_VERSION "Darwin Bootstrapper Version ${BOOT_VERSION}: ${BOOT_DATE}; Libxpc:${BOOT_DESC}/RELEASE_${MACHINE}"' \
	    > $@

${SHIMDIR}/systemstats/systemstats.h: ${SHIMS}/systemstats/systemstats.h
	@mkdir -p ${SHIMDIR}/systemstats
	cp ${SHIMS}/systemstats/systemstats.h ${SHIMDIR}/systemstats/systemstats.h

.for _s in ${BSM_SRCS}
${OBJDIR}/bsm/${_s:T:R}.o: ${_s}
	@mkdir -p ${.TARGET:H}
	${CC} ${BSM_CFLAGS} -c ${_s} -o ${.TARGET}
.endfor

${OBJDIR}/auditd_lib.o: ${OPENBSM_UPSTREAM}/libauditd/auditd_lib.c
	@mkdir -p ${.TARGET:H}
	${CC} ${BSM_CFLAGS} -c ${OPENBSM_UPSTREAM}/libauditd/auditd_lib.c -o ${.TARGET}

${OBJDIR}/systemstats_stub.o: ${SHIMS}/systemstats_stub.c
	@mkdir -p ${.TARGET:H}
	${CC} ${BSM_CFLAGS} -c ${SHIMS}/systemstats_stub.c -o ${.TARGET}

${LAUNCHCTL}: ${APPLE_LAUNCHCTL} ${LIBS} ${SHIMDIR}/CoreFoundation \
    ${SHIMDIR}/systemstats/systemstats.h ${SHIMDIR}/xpc_build_version.h \
    ${BSM_OBJS} \
    ${OBJDIR}/auditd_lib.o ${OBJDIR}/systemstats_stub.o
	@mkdir -p ${.TARGET:H}
	${CC} ${LAUNCHCTL_CFLAGS} ${APPLE_LAUNCHCTL} ${BSM_OBJS} \
	    ${OBJDIR}/auditd_lib.o ${OBJDIR}/systemstats_stub.o \
	    -L${RELEASE} -lxpc -Wl,-rpath,${RELEASE} \
	    -framework CoreFoundation -framework IOKit \
	    -Wl,-U,_readline -Wl,-U,__CFConstantStringClassReference \
	    -Wl,-U,__kCFSystemVersionBuildVersionKey -o $@

# Test-only, not shipped: the clean-room launchctl is the e2e harness's XPC
# client, and launchd_stub is the in-process launchd it talks to.  Shipped
# launchctl is Apple's, built above.
${TESTCTL}: src/launchctl/launchctl.c src/libxpc/include/xpc.h ${LIBS}
	@mkdir -p ${.TARGET:H}
	${CC} ${CFLAGS} src/launchctl/launchctl.c -L${RELEASE} -lxpc \
	    -Wl,-rpath,${RELEASE} -o $@

# The connection/session duplex round-trip test: one process plays both the
# listener and the client, so it needs no launchd machinery.
TESTCONN := ${TESTDIR}/test-connections
${TESTCONN}: tools/test-connections.c src/libxpc/include/xpc.h ${LIBS}
	@mkdir -p ${.TARGET:H}
	${CC} ${CFLAGS} tools/test-connections.c -L${RELEASE} -lxpc \
	    -Wl,-rpath,${RELEASE} -o $@

${LAUNCHD}: src/launchd/launchd_stub.c src/launchctl/launchctl.c \
    src/libxpc/include/xpc.h ${LIBS} ${TESTCTL}
	@mkdir -p ${.TARGET:H}
	${CC} ${CFLAGS} -DXNUXPORTS_EMBED -c src/launchctl/launchctl.c \
	    -o ${OBJDIR}/launchctl_embed.o
	${CC} ${CFLAGS} src/launchd/launchd_stub.c ${OBJDIR}/launchctl_embed.o \
	    -L${RELEASE} -lxpc -lpthread \
	    -Wl,-rpath,${RELEASE} -o $@

# XPC.framework is a re-export umbrella, like Apple's: a thin dylib whose
# only load command is LC_REEXPORT_DYLIB of our libxpc.
${FRAMEWORK}: ${LIBS} ${RELEASE}
	@mkdir -p $@/Versions/A/Headers $@/Versions/A/Modules $@/Versions/A/Resources
	${CC} -dynamiclib -install_name @rpath/XPC.framework/Versions/A/XPC \
	    -Wl,-reexport_library,${LIBS} -o $@/Versions/A/XPC
	cp src/libxpc/include/xpc.h $@/Versions/A/Headers/
	cp src/libxpc/module.modulemap $@/Versions/A/Modules/
	cp src/libxpc/Info.plist $@/Versions/A/Resources/
	ln -sfh A $@/Versions/Current
	ln -sfh Versions/Current/Headers $@/Headers
	ln -sfh Versions/Current/Modules $@/Modules
	ln -sfh Versions/Current/Resources $@/Resources
	ln -sfh Versions/Current/XPC $@/XPC

release: ${FRAMEWORK}

test: all ${LAUNCHD} ${TESTCTL} ${TESTCONN}
	sh tools/e2e-launchd.sh
	${TESTCONN}

clean:
	${RM} ${BUILD}
