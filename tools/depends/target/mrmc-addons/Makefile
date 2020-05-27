-include ../../Makefile.include
DEPS= ../../Makefile.include Makefile

# lib name
LIBNAME=master
ARCHIVE=$(LIBNAME).tar.gz
BASE_URL=https://github.com/MrMC/addons/archive

# No versioning, so always download locally in this (Makefile) directory
TARBALLS_LOCATION:=$(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))

CLEAN_FILES=$(ARCHIVE) $(PLATFORM)

all: .installed-$(PLATFORM)

$(PLATFORM): $(DEPS)
	cd $(TARBALLS_LOCATION); $(RETRIEVE_TOOL) $(RETRIEVE_TOOL_FLAGS) $(BASE_URL)/$(ARCHIVE)
	rm -rf $(PLATFORM); mkdir -p $(PLATFORM)
	cd $(PLATFORM); $(ARCHIVE_TOOL) $(ARCHIVE_TOOL_FLAGS) $(TARBALLS_LOCATION)/$(ARCHIVE)
	rm -f $(PLATFORM)/README.md

.installed-$(PLATFORM): $(PLATFORM)
# 	rm -rf $(PREFIX)/mrmc/repo-resources
# 	mkdir -p $(PREFIX)/mrmc/repo-resources
	cp -rf $(PLATFORM)/* $(PREFIX)/mrmc/addons
	touch $@

clean:
	rm -f .installed-$(PLATFORM)

distclean::
	rm -rf $(PLATFORM) .installed-$(PLATFORM) $(PREFIX)/mrmc/addons

distcleanfiles::
	rm -rf $(PLATFORM) .installed-$(PLATFORM) $(PREFIX)/mrmc/addons $(TARBALLS_LOCATION)/$(ARCHIVE)
