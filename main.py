#!/usr/bin/env python
import dis
from mydynamo._eval_frame import set_eval_frame
from mydynamo import eval_frame

def dummy(frame, catch_size):
    print('=' * 80)
    print(f"inside dummy, catch_size: {catch_size}  frame: {frame}")
    bc = dis.Bytecode(frame.f_code)
    [print(i) for i in bc]
    print(f"processing frame:\n{bc.info()}\n{bc.dis()}")
    print('=' * 80)

dummy_context = eval_frame.context(dummy)

@dummy_context
def add(a, b):
    return a + b

add(1, 2)
