#!/usr/bin/env python

from setuptools import setup, Extension

setup(name='mydynamo',
      version='0.1',
      packages=["mydynamo"],
      ext_modules=[Extension('mydynamo._eval_frame', [
            'mydynamo/_eval_frame.c',
      ])])
