objects := $(patsubst %.c,%.o,$(wildcard *.c)) israsm.o context.o syscall.o sched.o ustringasm.o
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
context.o: context.asm
	nasm -f elf64 -o $@ $<
	mkdir -p $(OBJPATH)
	cp $@ $(OBJPATH)/$@
syscall.o: syscall.asm
	nasm -f elf64 -o $@ $<
	mkdir -p $(OBJPATH)
	cp $@ $(OBJPATH)/$@
sched.o: sched.asm
	nasm -f elf64 -o $@ $<
	mkdir -p $(OBJPATH)
	cp $@ $(OBJPATH)/$@

ustringasm.o: ustring.asm
	nasm -f elf64 -o $@ $<
	mkdir -p $(OBJPATH)
	cp $@ $(OBJPATH)/$@
