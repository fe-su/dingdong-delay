# Makefile — DingDong Delay LV2 Plugin
# Käyttö:
#   make               — käännä
#   make install       — asenna /usr/lib/lv2/ (vaatii sudo)
#   make deploy        — käännä + asenna + restart pipedald
#   make clean         — siivoa

BUNDLE  = dingdong-delay.lv2
CC      = gcc
CFLAGS  = -O2 -fPIC -Wall -Wno-unused-parameter
LDFLAGS = -shared -lm

# Raspberry Pi 4/5 (aarch64, 64-bit Raspberry Pi OS):
CFLAGS += -march=armv8-a+crc -mtune=cortex-a72

SYSTEM_LV2 = /usr/lib/lv2

all: dingdong.so

dingdong.so: dingdong.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

install: all
	sudo mkdir -p $(SYSTEM_LV2)/$(BUNDLE)
	sudo cp dingdong.so manifest.ttl dingdong.ttl $(SYSTEM_LV2)/$(BUNDLE)/
	sudo chmod -R 755 $(SYSTEM_LV2)/$(BUNDLE)
	sudo chmod 644 $(SYSTEM_LV2)/$(BUNDLE)/*
	@echo "✓ Asennettu: $(SYSTEM_LV2)/$(BUNDLE)/"

deploy: install
	sudo systemctl restart pipedald
	@echo "✓ PiPedal käynnistetty uudelleen."

uninstall:
	sudo rm -rf $(SYSTEM_LV2)/$(BUNDLE)

clean:
	rm -f dingdong.so

.PHONY: all install deploy uninstall clean
