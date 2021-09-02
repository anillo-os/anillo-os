# Minimal makefile for Sphinx documentation
#

# You can set these variables from the command line, and also
# from the environment for the first two.
SPHINXOPTS    ?=
SPHINXBUILD   ?= sphinx-build
ARCH          ?= $(shell uname -m)
SOURCEDIR     = .
BUILDDIR      = $(SOURCEDIR)/../../build/$(ARCH)/kernel/breathe

ARCH_LIST := x86_64 aarch64

all:
	@$(foreach arch, $(ARCH_LIST), \
		make ARCH=$(arch) public; \
		make ARCH=$(arch) private; \
	)

# Put it first so that "make" without argument is like "make help".
help:
	@$(SPHINXBUILD) -M help "$(SOURCEDIR)" "$(BUILDDIR)/public" $(SPHINXOPTS) $(O)

.PHONY: help Makefile public private all

public:
	@$(SPHINXBUILD) -b html -t $(ARCH) -t public "$(SOURCEDIR)" "$(BUILDDIR)/public" $(SPHINXOPTS) $(O)

private:
	@$(SPHINXBUILD) -b html -t $(ARCH) -t private "$(SOURCEDIR)" "$(BUILDDIR)/private" $(SPHINXOPTS) $(O)

# Catch-all target: route all unknown targets to Sphinx using the new
# "make mode" option.  $(O) is meant as a shortcut for $(SPHINXOPTS).
%: Makefile
	@$(SPHINXBUILD) -M $@ "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)
