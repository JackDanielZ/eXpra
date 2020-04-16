APP_NAME = eXpra
PREFIX = /opt/mine

default: build/$(APP_NAME)

CFLAGS := -Wall -Wextra -Wshadow -Wno-type-limits -g3 -O0 -Wpointer-arith -fvisibility=hidden

CFLAGS += -DAPP_NAME=\"$(APP_NAME)\" -DPREFIX=\"$(PREFIX)\"

build/$(APP_NAME): main.c
	mkdir -p $(@D)
	gcc -g $^ $(CFLAGS) `pkg-config --cflags --libs elementary` -o $@

install: build/$(APP_NAME)
	mkdir -p $(PREFIX)/bin
	mkdir -p $(PREFIX)/share/$(APP_NAME)
	install -c build/$(APP_NAME) $(PREFIX)/bin/
	install -c -m 644 images/* $(PREFIX)/share/$(APP_NAME)

clean:
	rm -rf build/
