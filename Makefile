ifdef COMSPEC
DOTEXE:=.exe
else
DOTEXE:=
endif


CFLAGS:=-s -Ofast -Wall -Wextra
ifdef COMSPEC
CLIBS:=-lz -liconv
else
CLIBS:=-lz
endif


.PHONY: default clean
default: makegsf$(DOTEXE)
clean:
	-$(RM) makegsf makegsf.exe


%$(DOTEXE): %.c
	$(CC) $(CFLAGS) -o $@ $< $(CLIBS)

