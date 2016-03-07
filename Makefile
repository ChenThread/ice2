# I personally don't care if you steal this makefile. --GM

SOSUFFIX=so

CFLAGS = -g -O3 -flto -I. -std=gnu99 -Wall -Wextra -pthread -march=native -mtune=native
LDFLAGS = -g -flto -pthread
LIBS = -lavcodec -lavformat -lswscale -lavutil -lswresample
BINNAME = ice2
OBJDIR = obj

include Makefile.common

