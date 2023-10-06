# -*- coding: utf-8 -*-

# [DEPRECATED] ToDo python bindings are currently not supported

from __future__ import print_function

import sys

try:
    from skbuild import setup
except ImportError:
    print(
        "Please update pip, you need pip 10 or greater,\n"
        " or you need to install the PEP 518 requirements in pyproject.toml yourself",
        file=sys.stderr,
    )
    raise

from setuptools import find_packages

setup(
    name="vvv",
    version="0.0.1",
    description="Volcanite Renderer",
    author="Max Piochowiak",
    license="unlicensed",
    packages=find_packages(where='pyvvv/src'),
    package_dir={"": "pyvvv/src"},
    cmake_install_dir="pyvvv/src/vvv",
    package_data={
        "vvv": ['data/**/*.*'],
    },
    extras_require={"test": ["pytest"]},
)
