CC = gcc
CFLAGS = -Wall -Wextra -O2
LIBS = -lcurl
TARGET = pokedex

all: $(TARGET)

$(TARGET): main.c cJSON.c
	$(CC) $(CFLAGS) -o $(TARGET) main.c cJSON.c $(LIBS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/

uninstall:
	rm -f /usr/local/bin/$(TARGET)
