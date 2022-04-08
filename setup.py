from distutils.core import setup, Extension
import os
import sys

bluesky = Extension(name='bluesky',
                    sources=['src/network.c', 'src/bluesky.c'],
                    include_dirs=[f"{os.getcwd()}/3rd/jemalloc/include/jemalloc", f"{os.getcwd()}/3rd/libevent/include", f"{os.getcwd()}/src"],
                    extra_objects=[f"{os.getcwd()}/3rd/jemalloc/lib/libjemalloc.a", f"{os.getcwd()}/3rd/libevent/.libs/libevent.a"],
                    )


setup(name='bluesky',
      version='1.0',
      description='This is a bluesky package',
      ext_modules=[bluesky])
