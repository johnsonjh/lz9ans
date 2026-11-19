# Copyright (c) 2025-2026 Jeffrey H. Johnson <johnsonjh.dev@gmail.com>
# Copyright (c) 2025-2026 The DPS8M Development Team
# SPDX-License-Identifier: MIT
# scspell-id: 3f68d9c8-e37f-11f1-a47d-80ee73e9b8e7
##############################################################################

.PHONY: all
all: lz9ans_wp.pdf

.PHONY: clean
clean:
	rm -f lz9ans_wp.pdf

lz9ans_wp.pdf: src/lz9ans_wp.md
	pandoc -s $< --pdf-engine=xelatex -o $@

.PHONY: automatic
automatic: src/lz9ans_wp.md
	while inotifywait -e close_write $< || true; do $(MAKE) all; done

##############################################################################
# Local Variables:
# mode: makefile
# indent-tabs-mode: t
# tab-width: 4
# whitespace-style: (tabs tab-mark)
# whitespace-display-mappings: ((tab-mark 9 [45] [45]))
# fill-column: 78
# eval: (setq-local whitespace-display-mappings
#                   '((tab-mark 9
#                               [45 45 62]
#                               [45 45 62])))
# eval: (whitespace-mode 1)
# eval: (setq-local display-fill-column-indicator-column 78)
# eval: (display-fill-column-indicator-mode 1)
# End:
##############################################################################
# vim: set ft=make ts=4 ai noexpandtab list listchars=tab\:\>\- cc=78 :
##############################################################################
