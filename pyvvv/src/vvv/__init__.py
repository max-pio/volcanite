# -*- coding: utf-8 -*-
from .pyvvv_core import *
from .pyvvv_core import __version__, __doc__

import subprocess, os, platform

# sets the data directories to the current package path. Without this, the build will try to include files
# from a no-longer-existing temporary directory that was used to perform the CMake build.
shaderDirectory = os.path.join(os.path.dirname(__file__), "data/shader/")
setShaderIncludeDirectory(shaderDirectory)
dataDirectory = os.path.join(os.path.dirname(__file__), "data/")
setDataDirectory(dataDirectory)


def editFile(filepath):
    if platform.system() == 'Darwin':  # macOS
        subprocess.call(('open', filepath))
    elif platform.system() == 'Windows':  # Windows
        os.startfile(filepath)
    else:  # linux variants
        subprocess.call(('xdg-open', filepath))


def editShader(filepath):
    shaderpath = os.path.join(getShaderIncludeDirectory(), filepath)
    editFile(shaderpath)
