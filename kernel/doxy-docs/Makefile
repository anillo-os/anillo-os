DOXY_SOURCEDIR := $(shell pwd)
SOURCEDIR := $(DOXY_SOURCEDIR)/../..
ARCH_LIST := x86_64 aarch64

all:
	@$(foreach arch, $(ARCH_LIST), \
		cd "$(SOURCEDIR)" && doxygen $(DOXY_SOURCEDIR)/Doxyfile.$(arch).public; \
		cd "$(SOURCEDIR)" && doxygen $(DOXY_SOURCEDIR)/Doxyfile.$(arch).private; \
	)

.PHONY: all
