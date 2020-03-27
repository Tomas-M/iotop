TARGET=iotop
OBJS=main.o ioprio.o utils.o views.o xxxid_info.o checks.o vmstat.o arr.o
CFLAGS=-Wall -O2 --pedantic --std=c99
LDFLAGS=-lncurses

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

