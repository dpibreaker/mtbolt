OBJ	=	objs
DEP	=	dep
EXE = ${OBJ}/bin

COMMIT := $(shell git log -1 --pretty=format:"%H")
VERSION := $(shell git describe --tags --match 'v*' --abbrev=0 2>/dev/null | sed 's/^v//' || echo "unknown")
ifneq ($(EXTRA_VERSION),)
VERSION := $(EXTRA_VERSION)
endif

BITNESS_FLAGS =
ifeq ($(m), 32)
BITNESS_FLAGS = -m32
endif
ifeq ($(m), 64)
BITNESS_FLAGS = -m64
endif

# Determine the host architecture
HOST_ARCH := $(shell uname -m)

# Default CFLAGS and LDFLAGS
COMMON_CFLAGS := -O3 -std=gnu11 -Wall -fno-strict-aliasing -fno-strict-overflow -fwrapv -fno-common -DAES=1 -DCOMMIT=\"${COMMIT}\" -DVERSION=\"${VERSION}\" -D_FILE_OFFSET_BITS=64 -Wno-array-bounds -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE

# Platform-specific flags
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S), Darwin)
  # macOS: no -lrt (in libc), no -rdynamic (use -Wl,-export_dynamic or omit),
  # epoll-shim provides sys/epoll.h via kqueue, Homebrew OpenSSL is keg-only
  # Use the interpose variant to avoid macro conflicts (close, read, write)
  EPOLL_SHIM_CFLAGS := $(shell pkg-config --cflags epoll-shim-interpose 2>/dev/null)
  EPOLL_SHIM_LDFLAGS := $(shell pkg-config --libs epoll-shim-interpose 2>/dev/null)
  ifeq ($(EPOLL_SHIM_LDFLAGS),)
    $(error macOS build requires epoll-shim: brew install epoll-shim openssl)
  endif
  OPENSSL_CFLAGS := $(shell pkg-config --cflags openssl 2>/dev/null)
  OPENSSL_LDFLAGS := $(shell pkg-config --libs openssl 2>/dev/null)
  COMMON_CFLAGS += -D_GNU_SOURCE=1 $(EPOLL_SHIM_CFLAGS) $(OPENSSL_CFLAGS)
  COMMON_LDFLAGS := -ggdb -lm -lz -lpthread $(EPOLL_SHIM_LDFLAGS) $(OPENSSL_LDFLAGS)
else
  # Linux
  COMMON_CFLAGS += -D_GNU_SOURCE=1
  COMMON_LDFLAGS := -ggdb -rdynamic -pie -Wl,-z,relro,-z,now -lm -lrt -lcrypto -lz -lpthread
endif

# Auto-detect libunwind for stack traces on musl/Alpine (test/CI builds)
LIBUNWIND_CFLAGS := $(shell pkg-config --cflags libunwind 2>/dev/null)
LIBUNWIND_LDFLAGS := $(shell pkg-config --libs libunwind 2>/dev/null)
ifneq ($(LIBUNWIND_LDFLAGS),)
COMMON_CFLAGS += -DHAVE_LIBUNWIND $(LIBUNWIND_CFLAGS)
COMMON_LDFLAGS += $(LIBUNWIND_LDFLAGS)
endif

# Auto-detect libmaxminddb for GeoIP country metrics
MAXMINDDB_CFLAGS := $(shell pkg-config --cflags libmaxminddb 2>/dev/null)
MAXMINDDB_LDFLAGS := $(shell pkg-config --libs libmaxminddb 2>/dev/null)
ifneq ($(MAXMINDDB_LDFLAGS),)
COMMON_CFLAGS += -DHAVE_MAXMINDDB $(MAXMINDDB_CFLAGS)
COMMON_LDFLAGS += $(MAXMINDDB_LDFLAGS)
endif

# Auto-detect jemalloc for improved memory allocation
JEMALLOC_CFLAGS := $(shell pkg-config --cflags jemalloc 2>/dev/null)
JEMALLOC_LDFLAGS := $(shell pkg-config --libs jemalloc 2>/dev/null)
ifneq ($(JEMALLOC_LDFLAGS),)
COMMON_CFLAGS += -DHAVE_JEMALLOC $(JEMALLOC_CFLAGS)
COMMON_LDFLAGS += $(JEMALLOC_LDFLAGS)
endif

# Support additional flags (e.g. sanitizers): make EXTRA_CFLAGS="-fsanitize=address"
COMMON_CFLAGS += $(EXTRA_CFLAGS)
COMMON_LDFLAGS += $(EXTRA_LDFLAGS)

# Architecture-specific CFLAGS
ifeq ($(HOST_ARCH), x86_64)
CFLAGS := $(COMMON_CFLAGS) -mpclmul -march=core2 -mfpmath=sse -mssse3 $(BITNESS_FLAGS)
else ifeq ($(HOST_ARCH), aarch64)
CFLAGS := $(COMMON_CFLAGS) $(BITNESS_FLAGS)
else ifeq ($(HOST_ARCH), arm64)
CFLAGS := $(COMMON_CFLAGS) $(BITNESS_FLAGS)
endif

# Architecture-specific LDFLAGS (if needed, here kept same for simplicity)
LDFLAGS := $(COMMON_LDFLAGS)

LIB = ${OBJ}/lib
CINCLUDE = -iquote src/common -iquote src/common/toml -iquote src/common/qrcode -iquote src -iquote .

LIBLIST = ${LIB}/libkdb.a

PROJECTS = src/common src/common/toml src/common/qrcode src/jobs src/mtproto src/net src/crypto src/engine src/vv

OBJDIRS := ${OBJ} $(addprefix ${OBJ}/,${PROJECTS}) ${EXE} ${LIB}
DEPDIRS := ${DEP} $(addprefix ${DEP}/,${PROJECTS})
ALLDIRS := ${DEPDIRS} ${OBJDIRS}


.PHONY:	all clean lint test-dc-lookup fuzz fuzz-run

EXELIST	:= ${EXE}/mtbolt


OBJECTS	=	\
  ${OBJ}/src/mtproto/mtproto-proxy.o ${OBJ}/src/mtproto/mtproto-config.o ${OBJ}/src/mtproto/mtproto-dc-table.o ${OBJ}/src/mtproto/ip-stats.o ${OBJ}/src/mtproto/mtproto-check.o ${OBJ}/src/mtproto/mtproto-link.o ${OBJ}/src/net/net-tcp-rpc-ext-server.o

DEPENDENCE_CXX		:=	$(subst ${OBJ}/,${DEP}/,$(patsubst %.o,%.d,${OBJECTS_CXX}))
DEPENDENCE_STRANGE	:=	$(subst ${OBJ}/,${DEP}/,$(patsubst %.o,%.d,${OBJECTS_STRANGE}))
DEPENDENCE_NORM	:=	$(subst ${OBJ}/,${DEP}/,$(patsubst %.o,%.d,${OBJECTS}))

LIB_OBJS_NORMAL := \
	${OBJ}/src/common/crc32c.o \
	${OBJ}/src/common/pid.o \
	${OBJ}/src/common/sha1.o \
	${OBJ}/src/common/sha256.o \
	${OBJ}/src/common/md5.o \
	${OBJ}/src/common/resolver.o \
	${OBJ}/src/common/parse-config.o \
	${OBJ}/src/crypto/aesni256.o \
	${OBJ}/src/jobs/jobs.o ${OBJ}/src/common/mp-queue.o \
	${OBJ}/src/net/net-events.o ${OBJ}/src/net/net-msg.o ${OBJ}/src/net/net-msg-buffers.o \
	${OBJ}/src/net/net-config.o ${OBJ}/src/net/net-crypto-aes.o ${OBJ}/src/net/net-crypto-dh.o ${OBJ}/src/net/net-timers.o \
	${OBJ}/src/net/net-connections.o \
	${OBJ}/src/net/net-rpc-targets.o \
	${OBJ}/src/net/net-tcp-connections.o ${OBJ}/src/net/net-tcp-drs.o ${OBJ}/src/net/net-tcp-rpc-common.o ${OBJ}/src/net/net-tcp-rpc-client.o ${OBJ}/src/net/net-tcp-rpc-server.o \
	${OBJ}/src/net/net-http-server.o ${OBJ}/src/net/net-http-parse.o ${OBJ}/src/net/net-tls-parse.o ${OBJ}/src/net/net-obfs2-parse.o ${OBJ}/src/net/net-ip-acl.o \
	${OBJ}/src/common/tl-parse.o ${OBJ}/src/common/common-stats.o \
	${OBJ}/src/engine/engine.o ${OBJ}/src/engine/engine-signals.o \
	${OBJ}/src/engine/engine-net.o \
	${OBJ}/src/engine/engine-rpc.o \
	${OBJ}/src/engine/engine-rpc-common.o \
	${OBJ}/src/net/net-thread.o ${OBJ}/src/net/net-stats.o ${OBJ}/src/common/proc-stat.o \
	${OBJ}/src/common/kprintf.o \
	${OBJ}/src/common/precise-time.o ${OBJ}/src/common/cpuid.o \
	${OBJ}/src/common/server-functions.o ${OBJ}/src/common/crc32.o \
	${OBJ}/src/common/toml/tomlc17.o ${OBJ}/src/common/toml-config.o \
	${OBJ}/src/common/qrcode/qrcodegen.o \
	${OBJ}/src/vv/vv-tree.o \

LIB_OBJS := ${LIB_OBJS_NORMAL}

DEPENDENCE_LIB	:=	$(subst ${OBJ}/,${DEP}/,$(patsubst %.o,%.d,${LIB_OBJS}))

DEPENDENCE_ALL		:=	${DEPENDENCE_NORM} ${DEPENDENCE_STRANGE} ${DEPENDENCE_LIB}

OBJECTS_ALL		:=	${OBJECTS} ${LIB_OBJS}

all:	${ALLDIRS} ${EXELIST} 
dirs: ${ALLDIRS}
create_dirs_and_headers: ${ALLDIRS} 

${ALLDIRS}:	
	@test -d $@ || mkdir -p $@

-include ${DEPENDENCE_ALL}

${OBJECTS}: ${OBJ}/%.o: %.c | create_dirs_and_headers
	${CC} ${CFLAGS} ${CINCLUDE} -c -MP -MD -MF ${DEP}/$*.d -MQ ${OBJ}/$*.o -o $@ $<

${LIB_OBJS_NORMAL}: ${OBJ}/%.o: %.c | create_dirs_and_headers
	${CC} ${CFLAGS} -fpic ${CINCLUDE} -c -MP -MD -MF ${DEP}/$*.d -MQ ${OBJ}/$*.o -o $@ $<

${EXELIST}: ${LIBLIST}

${EXE}/mtbolt:	${OBJ}/src/mtproto/mtproto-proxy.o ${OBJ}/src/mtproto/mtproto-config.o ${OBJ}/src/mtproto/mtproto-dc-table.o ${OBJ}/src/mtproto/ip-stats.o ${OBJ}/src/mtproto/mtproto-check.o ${OBJ}/src/mtproto/mtproto-link.o ${OBJ}/src/net/net-tcp-rpc-ext-server.o ${OBJ}/src/mtproto/mtbolt-config.o
	${CC} -o $@ $^ ${LDFLAGS}

${LIB}/libkdb.a: ${LIB_OBJS}
	rm -f $@ && ar rcs $@ $^

clean:
	rm -rf ${OBJ} ${DEP} ${EXE} || true

lint:
	cppcheck --enable=warning,portability,performance \
	  --error-exitcode=1 \
	  --suppressions-list=.cppcheck-suppressions \
	  --suppress=missingIncludeSystem \
	  --std=c11 -I src/common -I src -I . \
	  src/common/ src/jobs/ src/mtproto/ src/net/ src/crypto/ src/engine/

force-clean: clean

test-dc-lookup:
	$(MAKE) -C fuzz test

FUZZ_DURATION ?= 60

fuzz:
	$(MAKE) -C fuzz

fuzz-run:
	$(MAKE) -C fuzz run FUZZ_DURATION=$(FUZZ_DURATION)
