SRCDIR=src
BINDIR=bin

SRC=$(shell find $(SRCDIR) -name *.c | sed 's/^$(SRCDIR)\///g')
OBJ=$(patsubst %.c,$(BINDIR)/%.o,$(SRC))

CFLAGS=-Wall -Werror

LDFLAGS+=-ljpeg -lpthread 

BIN=rpi-webcam

vpath %.c $(SRCDIR)
vpath %.o $(BINDIR)

all: debug

release: CFLAGS+=-O3
release: $(BIN)

debug: CFLAGS+=-g
debug: $(BIN)

$(BINDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BIN): $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

clean:
	@rm -rf $(BIN) $(BINDIR)


