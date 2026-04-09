SUBDIR := myos

.PHONY: all run clean

all:
	$(MAKE) -C $(SUBDIR) all

run:
	$(MAKE) -C $(SUBDIR) run

clean:
	$(MAKE) -C $(SUBDIR) clean