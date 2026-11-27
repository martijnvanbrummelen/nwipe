SUBDIRS = src man

# The set of files to be formatted.
FORMATSOURCES = src/*.c src/*.h

.PHONY: format check-format

format:
	clang-format -i -style=file $(FORMATSOURCES)

check-format:
	clang-format --dry-run --Werror -style=file $(FORMATSOURCES)
