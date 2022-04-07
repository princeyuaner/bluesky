
#jemalloc

JEMALLOC_STATICLIB := 3rd/jemalloc/lib/libjemalloc_pic.a

MALLOC_STATICLIB := $(JEMALLOC_STATICLIB)

$(JEMALLOC_STATICLIB) : 3rd/jemalloc/Makefile
	cd 3rd/jemalloc && make

3rd/jemalloc/autogen.sh :
	git submodule update --init

3rd/jemalloc/Makefile : | 3rd/jemalloc/autogen.sh
	cd 3rd/jemalloc && ./autogen.sh --with-jemalloc-prefix=je_ --enable-prof

all : jemalloc

jemalloc : $(MALLOC_STATICLIB)

#libevent

LIBEVENT_STATICLIB := 3rd/libevent/.libs/libevent.a

$(LIBEVENT_STATICLIB) : 3rd/libevent/autogen.sh
	cd 3rd/libevent && ./autogen.sh && ./configure && make

3rd/libevent/autogen.sh :
	git submodule update --init

all : libevent

libevent : $(LIBEVENT_STATICLIB)
