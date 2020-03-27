TARGET=iotop
OBJS=main.o ioprio.o utils.o views.o xxxid_info.o checks.o vmstat.o arr.o
CFLAGS=-Wall -O3 -std=gnu90 -fno-stack-protector -mno-stackrealign
LDFLAGS=-lncurses
ifndef NO_FLTO
CFLAGS+=-flto
LDFLAGS+=-flto -O3
endif
# -pedantic

PREFIX=/usr


$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: src/%.c
	$(CC) -c $(CFLAGS) -o $@ $<


.PHONY: clean install

clean:
	rm -f $(OBJS) $(TARGET)

install:
	cp $(TARGET) $(PREFIX)/bin/$(TARGET)

uninstall:
	rm $(PREFIX)/bin/$(TARGET)

