USING=main.o log.o capture.o buffer.o

ifeq ($(MODE),OMX)
#Using the GPU
USING+=jpeg_omx.o
INCLUDES+=-I/opt/vc/include
INCLUDES+=-I/opt/vc/include/interface/vcos/pthreads
INCLUDES+=-I/opt/vc/include/interface/vmcs_host/linux
INCLUDES+=-I/opt/vc/src/hello_pi/libs/ilclient

LDFLAGS+=-L/opt/vc/src/hello_pi/libs/ilclient -lilclient
LDFLAGS+=-L/opt/vc/lib -lopenmaxil -lbcm_host -lvcos -lvchiq_arm -lpthread -lrt
else
#Using the CPU
ifdef LIBJPEG
#Force another JPEG lib like: libjpeg-turbo
INCLUDES+=-I$(LIBJPEG)/include
LDFLAGS+=-L$(LIBJPEG)/lib
endif
USING+=jpeg_cpu.o
LDFLAGS+=-ljpeg
endif

OBJ=$(patsubst %,build/%,$(USING))

CFLAGS=-Wall -Werror

INCLUDES+=-Iinclude

LDFLAGS+=-lpthread 

BIN=bin/rpi-webcam

all: debug

release: CFLAGS+=-O3
release: $(BIN)

debug: CFLAGS+=-g
debug: $(BIN)

build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BIN): $(OBJ)
	@mkdir -p $(dir $@)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

clean:
	@rm -rf bin build


