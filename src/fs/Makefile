objects := $(patsubst %.c,%.o,$(wildcard *.c))
subdirs :=
OBJPATH=$(OBJDIR)/fs
export

all: $(subdirs) $(objects)
	for subdir in $(subdirs) ; do $(MAKE) -C $${subdir} ; done

%.o: %.c
	$(CC) $(CFLAGS) -o $@ $<
	mkdir -p $(OBJPATH) 
	cp $@ $(OBJPATH)/$@
