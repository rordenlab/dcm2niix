#!/usr/bin/env python
"""extract_units.py - extract BIDS/README.md's units as json

Usage:
  extract_units.py [-e EXISTING -o OUT] [MD]
  extract_units.py (-h|--help|--version)

Arguments:
  MD:      A Markdown input file including tables with Field and Unit as the
           first two columns.
           [default: README.md]

Options:
  -h --help                     Show this message and exit.
  --version                     Show version and exit.
  -e EXISTING --ex=EXISTING     Extract units for all the fields, and only the
                                fields in EXISTING, a BIDS file, and write the
                                output to
                                EXISTING.replace('.json', '_units.json')
                                instead of stdout.
  -o OUT --out=OUT              If given, save the output to this filename.
                                (Overrides the implicit destination of -e.)
"""
from __future__ import print_function
try:
    import json_tricks as json
except ImportError:
    try:
        import simplejson as json
    except ImportError:
        # Not really compatible with json_tricks because it does not support
        # primitives=True
        import json
import sys

# Please use semantic versioning (API.feature.bugfix), http://semver.org/
__version__ = '0.0.0'


def extract_units(mdfn):
    units = {}
    intable = False
    with open(mdfn) as md:
        for line in md:
            if line.startswith('|'):
                parts = [s.strip() for s in line.split('|')[1:-1]]
                if parts[:2] == ['Field', 'Unit']:
                    intable = True
                elif intable and parts[1] and not parts[1].startswith('-'):
                    units[parts[0]] = parts[1]
            else:
                intable = False
    return units


def main(mdfn, existing=None, outfn=None):
    units = extract_units(mdfn)
    if existing:
        with open(existing) as f:  # Can't count on having json_tricks.
            used = json.load(f)
        units = {k: v for k, v in units.items() if k in used}
        if not outfn:
            outfn = existing.replace('.json', '_units.json')
    outtext = json.dumps(units, indent=2, sort_keys=True,
                         separators=(', ', ': '))
    if outfn:
        with open(outfn, 'w') as outf:
            outf.write(outtext + "\n")
    else:
        print(outtext)
    return units


if __name__ == '__main__':
    from docopt import docopt

    args = docopt(__doc__, version=__version__)

    # https://github.com/docopt/docopt/issues/214 has been open for
    # almost 7 years, so it looks like docopt isn't getting default
    # positional args.
    output = main(args['MD'] or 'README.md',
                  args.get('--ex'), args.get('--out'))
    sys.exit(0)
