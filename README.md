# My Dynamo

Learning internals of torchdynamo

- [torchdynamo](https://github.com/pytorch/torchdynamo)
- [PEP 523](https://www.python.org/dev/peps/pep-0523/)

## Notes

- tinyvm.py is a very tiny script to demo a stack virtual machine that interpretes python bytecodes.
- naive_transform.py is demostrating howto transform bytecodes of `a + b` to `a * b + a + b` by utilizing pep523.
- paddle_fx_poc.py is a poc for creating a python IR for paddle python program by symbolic trace.

## Development Setup

initial setup
```
python setup.py develop  # compiles C/C++ extension
python naive_transform.py
```
