# Some make rules to make output pretty....

# default ARFLAGS also has 'v', but we don't want it to be verbose.
ARFLAGS= -r

# make sure libs from /usr/local/lib are found
VPATH= /lib64 /usr/lib64 /usr/local/lib64 /lib /usr/lib /usr/local/lib

LIBTOOL=libtool

OS= $(shell uname -s)
HW= $(shell uname -m)

ifneq ($(OS), FreeBSD)
FLEX=flex
else
FLEX=/usr/local/bin/flex
endif

BUILDCC:=${CC}
BUILDLIBTOOL:=${LIBTOOL}
ifdef BUILDSYS
BUILDCC:=${BUILDSYS}-gcc
BUILDLIBTOOL:=${BUILDSYS}-libtool
endif

ifdef HOSTSYS
CC=${HOSTSYS}-gcc
LIBTOOL=${HOSTSYS}-libtool
CONF_HOST=--host=${HOSTSYS}
HW=$(HOSTSYS)
endif


%.o : %.c
	@echo "     CC $<"
	@$(CC) -MMD $(CFLAGS) -c $< -o $@

%.o : %.il2c.c
	@echo "LT   CCil $<"
	@${LIBTOOL} --quiet --mode=compile --tag=CC $(CC) -MMD $(CFLAGS) -c $< -o $@
	@sed -e "s:\.il2c.c:\.il:" -i -i $*.il2c.d

%: %.o
	@echo "     LD $@"
	@${LIBTOOL} --quiet --mode=link --tag=CC $(LINK.o) $(filter %.o,$^) $(LOADLIBS) $(LDLIBS) $($@_LDFLAGS) -o $@

%.lo: %.c
	@echo "LT   CC $<"
	@${LIBTOOL} --quiet --mode=compile --tag=CC $(CC) -MMD $(CFLAGS) -c $< -o $@
	@cat $(dir $*).libs/$(*F).d | sed -e "s:\.libs/::" -e "s:\.o:\.lo:" > $*.d

%.lo: %.il2c.c
	@echo "LT  ilCC $<"
	@${LIBTOOL} --quiet --mode=compile --tag=CC $(CC) -MMD $(CFLAGS) -c $<
	@cat $(dir $*).libs/$(*F).d | sed -e "s:\.libs/::" -e "s:\.o:\.lo:" -e "s:\.il2c.c:\.il:" > $*.d

define LIB_LINK
	@echo "LT   LD $@"
	@${LIBTOOL} --quiet --mode=link --tag=CC $(CC) $(filter %.lo,$^) -o $@ $(LDFLAGS) $($@_LDFLAGS) -static-libtool-libs -rpath $(abspath $(@D))
	@echo "LT INST $@"
	@${LIBTOOL} --quiet --mode=install install $@ $(abspath $(@D))
	@sed -i -i  s\|=$(CURDIR)\|$(CURDIR)\|g $@ 
endef

%.so:
	@echo "LT soLD $@"
	@${LIBTOOL}  --quiet --mode=link --tag=CC $(CC) $(filter %.lo,$^) -o $@ $(LDFLAGS) $($@_LDFLAGS)

(%): %
	@echo "   AR  $^ in $@"
	@$(AR) $(ARFLAGS) $@ $^

%.tab.c %.tab.h: %.y
	@echo "BISON   $<"
	@bison --defines=$*.tab.h $< -o $*.tab.c

%.yy.c %.yy.h: %.l %.tab.h
	@echo "   FLEX $<"
	@$(FLEX) --header-file=$*.yy.h -o $*.yy.c $<

# il2c: instruction list 2 c 'compiler'
%.il2c.c: %.il
	@echo "   IL2C $<"
	@$(IL2C) $<

# dot -> pdf
%.pdf: %.dot
	@echo "  DOT  $<"
	@dot $< -o $@ -Tpdf

%.dtb: %.dts
	@echo "  DTC  $<"
	@dtc -I dts $< -O dtb -o $@

%.dtbo: %.dts
	@echo "  DTCo $<"
	@dtc -I dts $< -O dtb -o $@
