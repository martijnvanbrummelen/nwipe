AUTOMAKE_OPTIONS = foreign
SUBDIRS = src man
nwipe_SOURCES = src/*.c src/*.h

format:
	clang-format -i -style=file $(nwipe_SOURCES)

check-format:
	clang-format -i -style=file $(nwipe_SOURCES) && git diff-index --quiet HEAD
