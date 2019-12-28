AUTOMAKE_OPTIONS = foreign
SUBDIRS = src man
NWIPESOURCES = src/*.c src/*.h

format:
	clang-format -i -style=file $(NWIPESOURCES)

check-format:
	clang-format -i -style=file $(NWIPESOURCES) && git diff-index --quiet HEAD
