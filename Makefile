SSL_LIB=

SOURCES = tp3serv.c mongoose/mongoose.c
#CFLAGS = -g -W -Wall -I./mongoose -ljpeg -Wno-unused-function $(CFLAGS_EXTRA) -DMG_ENABLE_THREADS
#CFLAGS = -g -W -Wall -I./mongoose -ljpeg -Wno-unused-function $(CFLAGS_EXTRA)
CFLAGS = -O3 -W -Wall -I./mongoose -ljpeg -Wno-unused-function $(CFLAGS_EXTRA)

all: tp3serv

ifeq ($(OS), Windows_NT)
# TODO(alashkin): enable SSL in Windows
CFLAGS += -lws2_32
CC = gcc
else
#CFLAGS += -pthread
endif

ifeq ($(SSL_LIB),openssl)
CFLAGS += -DMG_ENABLE_SSL -lssl -lcrypto
endif
ifeq ($(SSL_LIB), krypton)
CFLAGS += -DMG_ENABLE_SSL ../../../krypton/krypton.c -I../../../krypton
endif
ifeq ($(SSL_LIB),mbedtls)
CFLAGS += -DMG_ENABLE_SSL -DMG_SSL_IF=MG_SSL_IF_MBEDTLS -DMG_SSL_MBED_DUMMY_RANDOM -lmbedcrypto -lmbedtls -lmbedx509
endif

ifeq ($(JS), yes)
	V7_PATH = ../../deps/v7
	CFLAGS_EXTRA += -DMG_ENABLE_JAVASCRIPT -I $(V7_PATH) $(V7_PATH)/v7.c
endif

tp3serv: $(SOURCES)
	$(CC) $(SOURCES) -o $@ $(CFLAGS)

tp3serv.exe: $(SOURCES)
	cl $(SOURCES) /I../.. /MD /Fe$@

clean:
	rm -rf *.gc* *.dSYM *.exe *.obj *.o a.out tp3serv
