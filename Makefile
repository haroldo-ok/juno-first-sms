DEVKIT   ?= $(HOME)/.devkitsms
PROG     ?= junofirst
CC       := sdcc
CFLAGS   := -mz80 -I$(DEVKIT)/include --peep-file $(DEVKIT)/include/peep-rules.txt -Isrc --max-allocs-per-node 20000 --opt-code-speed
LDFLAGS  := -mz80 --no-std-crt0 --data-loc 0xC000 -Wl-b_BANK2=0x28000 -Wl-b_BANK3=0x38000

all: $(PROG).sms

src/gfx.h src/gfxbank.c src/bossgfx.c: tools/gen_gfx.py
	python3 tools/gen_gfx.py src/gfx.h src/gfxbank.c src/bossgfx.c

bossgfx.rel: src/bossgfx.c
	$(CC) -mz80 --constseg BANK2 -c $< -o $@

gfxbank.rel: src/gfxbank.c
	$(CC) -mz80 --constseg BANK3 -c $< -o $@
src/snd.h src/snd.c: tools/gen_snd.py
	python3 tools/gen_snd.py src/snd.h src/snd.c

snd.rel: src/snd.c
	$(CC) -mz80 --constseg BANK2 -c $< -o $@

main.rel: src/main.c src/gfx.h src/snd.h
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: $(PROG).ihx
$(PROG).ihx: main.rel snd.rel bossgfx.rel gfxbank.rel
	$(CC) -o $@ $(LDFLAGS) $(DEVKIT)/lib/crt0_sms.rel main.rel $(DEVKIT)/lib/SMSlib.lib $(DEVKIT)/lib/PSGlib.lib snd.rel bossgfx.rel gfxbank.rel

$(PROG).sms: $(PROG).ihx
	ihx2sms $< $@

playtest: $(PROG).sms
	@mkdir -p shots
	python3 tests/check_rom.py $(PROG).noi $(PROG).sms src/bossgfx.c src/snd.c src/gfxbank.c
	python3 tests/mkplaytest.py $(PROG).noi tests/playtest.in > tests/playtest.txt
	smstest $(PROG).sms tests/playtest.txt
	smstest $(PROG).sms tests/demo.txt
	smstest $(PROG).sms tests/soak.txt
	python3 tests/mkplaytest.py $(PROG).noi tests/glasses.in > tests/glasses.txt
	smstest $(PROG).sms tests/glasses.txt
	python3 tests/mkplaytest.py $(PROG).noi tests/boss.in > tests/boss.txt
	smstest $(PROG).sms tests/boss.txt
	python3 tests/mkplaytest.py $(PROG).noi tests/ending.in > tests/ending.txt tests/radar.txt tests/opts3d.txt
	smstest $(PROG).sms tests/ending.txt tests/radar.txt tests/opts3d.txt
	python3 tests/mkplaytest.py $(PROG).noi tests/radar.in > tests/radar.txt
	smstest $(PROG).sms tests/radar.txt
	python3 tests/check_radar.py shots/radar.ppm
	python3 tests/mkplaytest.py $(PROG).noi tests/opts3d.in > tests/opts3d.txt
	smstest $(PROG).sms tests/opts3d.txt
	python3 tests/same_ppm.py shots/opts_a.ppm shots/opts_b.ppm

clean:
	rm -f *.rel *.ihx *.sym *.lst *.map *.noi *.lk *.asm *.rst $(PROG).sms src/gfx.h src/gfxbank.c src/bossgfx.c src/snd.h src/snd.c

.PHONY: all playtest clean
