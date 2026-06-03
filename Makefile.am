SUBDIRS = src man

# The set of files to be formatted.
FORMATSOURCES = src/*.c src/*.h src/opencl/*.c src/opencl/*.h
format:
	clang-format -i -style=file $(FORMATSOURCES)

check-format:
	clang-format -i -style=file $(FORMATSOURCES) && git diff --exit-code
