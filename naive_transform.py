import dis
import copy
from mydynamo import eval_frame

from mydynamo.bytecode_transformation import create_instruction
from mydynamo.bytecode_transformation import debug_checks
from mydynamo.bytecode_transformation import transform_code_object
from mydynamo.guards import GuardedCode

def mytransform(frame, catch_size):

    def insert_ops(instructions, code_options):
        # change a + b to a * b + a + b
        instructions.insert(1, create_instruction("BINARY_ADD"))        
        instructions.insert(0, create_instruction("BINARY_MULTIPLY"))
        instructions.insert(0, create_instruction("LOAD_FAST", arg=1, argval='b'))
        instructions.insert(0, create_instruction("LOAD_FAST", arg=0, argval='a'))

    #debug_checks(frame.f_code)
    return GuardedCode(transform_code_object(frame.f_code, insert_ops))

mytransform_context = eval_frame.context(mytransform)

@mytransform_context
def add(a, b):
    return a + b

def transformed_add(a, b):
    return a * b + a + b

A = 5
B = 6

print("transforming a + b to a * b + a + b")
print(add(A, B))
print("should be equal to")
print(transformed_add(A, B))
