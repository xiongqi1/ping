# default setting for local compile
ifeq ($(PLATFORM),Platypus)
CROSS_COMPILE ?= mipsel-linux-
INSTALLDIR    ?= ../../../romfs
CDCS_INCLUDE  ?= ../../../staging_l/include
CDCS_LIB      ?= ../../../staging_l/lib
else
CROSS_COMPILE ?= arm-linux-
INSTALLDIR    ?= ../../staging
CDCS_INCLUDE  ?= ../../staging_l/include
CDCS_LIB      ?= ../../staging_l/alibs
endif

# binary names
CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld
STRIP := $(CROSS_COMPILE)strip

FILE_DBLIB := $(CDCS_LIB)/rdb.a

# compile flags
ifdef DEBUG
	CFLAGS := -O0 -g -c -Wall -I$(CDCS_INCLUDE)
	# This disables strip
	STRIP  := echo
else
	CFLAGS := -c -Wall -Os -I$(CDCS_INCLUDE) -fPIE -fstack-protector-strong
	LFLAGS += -pie
endif

# turn off strip if debug
ifdef DEBUG
	STRIP = echo
endif

# common compile environment
CFLAGS += -I$(CDCS_INCLUDE)

# platform specific compile environment
ifeq ($(PLATFORM),Avian)
	CFLAGS += -DPLATFORM_AVIAN
else ifeq ($(PLATFORM),Bovine)
	CFLAGS += -DPLATFORM_BOVINE
else ifeq ($(PLATFORM),Platypus)
	CFLAGS += -DPLATFORM_PLATYPUS
else ifeq ($(PLATFORM),Antelope)
	CFLAGS += -DPLATFORM_ANTELOPE
else ifeq ($(PLATFORM),Arachnid)
	CFLAGS += -DPLATFORM_ARACHNID
endif

CFLAGS+= -DBOARD_$(V_BOARD) -DV_PINGMONITOR_$(V_PINGMONITOR)

ifeq ($(PLATFORM),Platypus)
	# platypus
	LIBS := -lm $(FILE_DBLIB)
	TEST_LIBS := $(LIBS) $(CDCS_LIB)/unit_test.a
	RTSDK = $(shell CURDIR=`pwd`; echo $${CURDIR%%/user/cdcs/periodicpingd})
	LIBNV_BIN = libnvram.a
	LIBNV_DIR = $(RTSDK)/lib/libnvram
	LIBNV_FNAME = $(LIBNV_DIR)/$(LIBNV_BIN)
	CFLAGS+=-I $(LIBNV_DIR)
	LIBS+=$(LIBNV_FNAME)
else
	# bovine / Avian
	LIBS := $(FILE_DBLIB)
	TEST_LIBS := $(LIBS) $(CDCS_LIB)/unit_test.a
endif

PROJECT = periodicpingd
SOURCES = periodicping.c ping.c daemon.c
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

.PHONY:	all clean distclean

clean distclean:
	if [ -z $(PROJECT) ]; then \
		echo WARNING: PROJECT environment not defined; \
	else \
		find -type f -name "*.[ch]" | xargs chmod 644 Makefile* $(PROJECT).*; \
		rm -f $(PROJECT) $(PROJECT).a $(OBJECTS) $(FILE_DD) $(FILE_RDB); \
		rm -f *~; \
		rm -f  ~*$(PROJECT).vcproj $(PROJECT).vcproj.*.user $(PROJECT).{suo,ncb}; \
		rm -fr Debug Release; \
	fi

%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

all:	$(PROJECT)

$(PROJECT): $(OBJECTS)
	$(CC) $(LFLAGS) $(OBJECTS) $(LIBS) -o $@
	$(STRIP) $@

$(SOURCES): $(FILE_RDB) $(FILE_DD)

install: $(PROJECT)
	$(STRIP) $(PROJECT)
	mkdir -p $(INSTALLDIR)/usr/bin
	cp $(PROJECT) $(INSTALLDIR)/usr/bin/$(PROJECT)
