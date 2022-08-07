objects := $(patsubst %.c,%.o,$(wildcard *.c)) israsm.o
subdirs=boot
OBJPATH=$(OBJDIR)/arch/${TARGET}/
export

all: $(objects) $(subdirs)
	for subdir in $(subdirs) ; do $(MAKE) -C $${subdir} ; done

%.o: %.c
	$(CC) $(CFLAGS) -o $@ $<
	mkdir -p $(OBJPATH) 
	cp $@ $(OBJPATH)/$@

israsm.o: isr.asm
	nasm -f elf64 -o $@ $<
	mkdir -p $(OBJPATH)
	cp $@ $(OBJPATH)/$@