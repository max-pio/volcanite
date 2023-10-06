# [DEPRECATED] Python Bindings

---
*The python bindings are currently untested and may not work.
They remain within the project to be either consolidated or replaced with another variant in a future version of the framework.*
---

**Installation:**
The python bindings are based on the [scikit_build_example](https://github.com/pybind/scikit_build_example). Build them
using

```
pip install .
```

Verify that the python bindings were successfully build and installed:

```
[rd@localhost vvv]$ python
Python 3.9.6 (default, Jul 16 2021, 00:00:00) 
[GCC 11.1.1 20210531 (Red Hat 11.1.1-3)] on linux
Type "help", "copyright", "credits" or "license" for more information.
>>> import vvv
>>> vvv.__version__
'0.1'
>>> vvv.isDebugBuild
False
```

**Debugging Bindings:** Sometimes, a command in the bindings might result in an exception ending
with `(compile in debug mode for details)`. To get the full error message, do the following:

Inspect the root `CMakeLists.txt` and make sure you are compiling pybind11 from scratch.

```
# to use a local copy, e.g. when you want to make a debug build of pybind:
add_subdirectory(extern/pybind11)
# to use the version from your package manager:
# find_package(pybind11 REQUIRED)
```

Then build the package with `pip install --install-option="--build-type=Debug" .` and verify that you are running the
debug build:

```
[rd@localhost vvv]$ python
Python 3.9.6 (default, Jul 16 2021, 00:00:00) 
[GCC 11.1.1 20210531 (Red Hat 11.1.1-3)] on linux
Type "help", "copyright", "credits" or "license" for more information.
>>> import vvv
>>> vvv.isDebugBuild
True
```

If you run out of memory during building of python modules, make sure you have enough temporary memory.
Check if temporary folder is the problem using `sudo df -h`. Then increase its size with `sudo mount -o remount,size=15G /tmp/`.