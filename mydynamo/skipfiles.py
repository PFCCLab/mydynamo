import os

SKIP_DIRS = [os.path.dirname(__file__) + "/"]

def check(filename):
    return any(filename.startswith(d) for d in SKIP_DIRS)
