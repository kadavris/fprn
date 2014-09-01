#!/bin/sh
echo '#include <sys/time.h>' > versioning.c
echo 'char *compiled_time = "Compiled at '`uname -snrvi`'\n    At '`date`'\n    Built with options: ' $1 '\n";' >> versioning.c
