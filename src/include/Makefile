objects := $(patsubst %.c,%.o,$(wildcard *.c))
subdirs := stdio string hashtable ringbuffer math
OBJPATH=$(OBJDIR)/include/
export

all: $(subdirs) $(objects)
	for subdir in $(subdirs) ; do $(MAKE) -C $${subdir} ; done

%.o: %.c
	$(CC) $(CFLAGS) -o $@ $<
	mkdir -p $(OBJPATH) 
	cp $@ $(OBJPATH)/$@
