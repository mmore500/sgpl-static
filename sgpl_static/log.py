"""Progress messages go to stderr; stdout is for the stripped JSON."""

import sys


def log(message):
    print(message, file=sys.stderr, flush=True)
