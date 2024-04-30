from distutils.core import setup, Extension
import os
import sys

bluesky = Extension(name='bluesky',
                    sources=['3rd/skynet/skynet_mq.c','src/bluesky_network.c', 'src/bluesky.c','src/bluesky_server.c','src/bluesky_timer.c'],
                    include_dirs=[f"{os.getcwd()}/3rd/jemalloc/include/jemalloc", f"{os.getcwd()}/3rd/libevent/include", f"{os.getcwd()}/src",f"{os.getcwd()}/3rd/skynet"],
                    extra_objects=[f"{os.getcwd()}/3rd/jemalloc/lib/libjemalloc.a", f"{os.getcwd()}/3rd/libevent/.libs/libevent.a"],
                    )


setup(name='bluesky',
      version='1.0',
      description='This is a bluesky package',
      ext_modules=[bluesky])
