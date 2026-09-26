CC      = gcc
CFLAGS  = -Wall -Wextra -O2
LDFLAGS = -lcurl
PREFIX  = /usr/local
TARGET  = pokedex
SRCS    = main.c cJSON.c

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(SRCS) cJSON.h
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)

clean:
	rm -f $(TARGET)
