DEVKIT   ?= $(HOME)/.devkitsms
PROG     ?= junofirst
CC       := sdcc
CFLAGS   := -mz80 -I$(DEVKIT)/include --peep-file $(DEVKIT)/include/peep-rules.txt -Isrc --max-allocs-per-node 20000 --opt-code-speed
LDFLAGS  := -mz80 --no-std-crt0 --data-loc 0xC000 -Wl-b_BANK2=0x28000

all: $(PROG).sms

src/gfx.h: tools/gen_gfx.py
	python3 tools/gen_gfx.py $@
src/snd.h src/snd.c: tools/gen_snd.py
	python3 tools/gen_snd.py src/snd.h src/snd.c

snd.rel: src/snd.c
	$(CC) -mz80 --constseg BANK2 -c $< -o $@

main.rel: src/main.c src/gfx.h src/snd.h
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: $(PROG).ihx
$(PROG).ihx: main.rel snd.rel
	$(CC) -o $@ $(LDFLAGS) $(DEVKIT)/lib/crt0_sms.rel main.rel $(DEVKIT)/lib/SMSlib.lib $(DEVKIT)/lib/PSGlib.lib snd.rel

$(PROG).sms: $(PROG).ihx
	ihx2sms $< $@

playtest: $(PROG).sms
	@mkdir -p shots
	python3 tests/mkplaytest.py $(PROG).noi tests/playtest.in > tests/playtest.txt
	smstest $(PROG).sms tests/playtest.txt
	smstest $(PROG).sms tests/demo.txt
	smstest $(PROG).sms tests/soak.txt

clean:
	rm -f *.rel *.ihx *.sym *.lst *.map *.noi *.lk *.asm *.rst $(PROG).sms src/gfx.h src/snd.h src/snd.c

.PHONY: all playtest clean
