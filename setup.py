from distutils.core import setup, Extension
import os
import sys

module1 = Extension('net',
                    sources=['src/net.c'],
                    include_dirs=[f"{os.getcwd()}/3rd/jemalloc/include/jemalloc", f"{os.getcwd()}/3rd/libevent/include", f"{os.getcwd()}/src"],
                    library_dirs=[f"{os.getcwd()}/3rd/jemalloc/lib/", f"{os.getcwd()}/3rd/libevent/.libs/"],
                    libraries=["event", "jemalloc"],
                    )

setup(name='PackageName',
      version='1.0',
      description='This is a net package',
      ext_modules=[module1])
