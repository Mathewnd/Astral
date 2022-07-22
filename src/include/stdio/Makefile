objects := $(patsubst %.c,%.o,$(wildcard *.c))
subdirs := 
OBJPATH=$(OBJDIR)/include/stdio/
export

all: $(subdirs) $(objects)

%.o: %.c
	$(CC) $(CFLAGS) -o $@ $<
	mkdir -p $(OBJPATH) 
	cp $@ $(OBJPATH)/$@
