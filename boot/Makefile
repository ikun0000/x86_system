boot.bin: boot.S
	nasm -I ../inc/ -f bin -o $@ $<

loader.bin: loader.S
	nasm -I ../inc/ -f bin -o $@ $<

clean:
	rm -rf *.bin
