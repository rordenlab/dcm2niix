"""Thin wrapper around dcm2niix binary"""
__author__ = "Casper da Costa-Luis"
__date__ = "2022"
# version detector. Precedence: installed dist, git, 'UNKNOWN'
try:
    from ._dist_ver import __version__
except ImportError: # pragma: nocover
    try:
        from setuptools_scm import get_version

        __version__ = get_version(root="../..", relative_to=__file__)
    except (ImportError, LookupError):
        __version__ = "UNKNOWN"
__all__ = ['bin', 'bin_path', 'main']

from pathlib import Path

bin_path = Path(__file__).resolve().parent / "dcm2niix"
bin = str(bin_path)


def main(args=None):
    if args is None:
        import sys
        args = sys.argv[1:]
    from subprocess import run
    run([bin] + args)
