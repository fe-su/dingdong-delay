# DingDong Delay — LV2 Plugin Makefile
#
# make          - build
# make install  - install to /usr/lib/lv2/ (requires sudo)
# make deploy   - build, install and restart PiPedal
# make clean    - remove build artifacts

BUNDLE     = dingdong-delay.lv2
CC         = gcc
CFLAGS     = -O2 -fPIC -Wall -Wno-unused-parameter -march=armv8-a+crc -mtune=cortex-a72
LDFLAGS    = -shared -lm
LV2_DIR    = /usr/lib/lv2

all: dingdong.so

dingdong.so: dingdong.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

install: all
	sudo mkdir -p $(LV2_DIR)/$(BUNDLE)
	sudo cp dingdong.so manifest.ttl dingdong.ttl $(LV2_DIR)/$(BUNDLE)/
	sudo chmod -R 755 $(LV2_DIR)/$(BUNDLE)
	sudo chmod 644 $(LV2_DIR)/$(BUNDLE)/*

deploy: install
	sudo systemctl restart pipedald

uninstall:
	sudo rm -rf $(LV2_DIR)/$(BUNDLE)

clean:
	rm -f dingdong.so

.PHONY: all install deploy uninstall clean
