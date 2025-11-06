import inspect
import re
import sys
from pathlib import Path

def get_group_module_name():
    stack = inspect.stack()

    # usually this is the file name of the calling module
    filename = stack[1][1]

    module = Path(filename).stem
    fullmodule = get_modulename_from_filename(filename)

    file_posix = Path(filename).as_posix()
    group = get_groupname(file_posix, module)

    return (group, module, fullmodule)

def get_modulename_from_filename(filename):
    mods = list(sys.modules.values())
    for mod in mods:
        if hasattr(mod, "__file__"):
            if mod.__file__ == filename:
                return mod.__name__

    return ""

def get_groupname(filename, module):
    match = re.search("/Mod/(\\w+)/", filename)
    if not match is None:
        return match.group(1)

    match = re.search("/Ext/freecad/(\\w+)/", filename)
    if not match is None:
        return match.group(1)

    return module
