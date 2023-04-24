"""a tiny python bytecode interpreter(a.k.a: virtual machine) for learning purpose"""
# simpified from:
# https://github.com/rocky/x-python

import dis
import operator

UNARY_OPERATORS = {
    "POSITIVE": operator.pos,
    "NEGATIVE": operator.neg,
    "NOT": operator.not_,
    "CONVERT": repr,
    "INVERT": operator.invert,
}

BINARY_OPERATORS = {
    "POWER": pow,
    "MULTIPLY": operator.mul,
    "DIVIDE": getattr(operator, "div", lambda x, y: x / y),
    "FLOOR_DIVIDE": operator.floordiv,
    "TRUE_DIVIDE": operator.truediv,
    "MODULO": operator.mod,
    "ADD": operator.add,
    "SUBTRACT": operator.sub,
    "SUBSCR": operator.getitem,
    "LSHIFT": operator.lshift,
    "RSHIFT": operator.rshift,
    "AND": operator.and_,
    "XOR": operator.xor,
    "OR": operator.or_,
}


class TinyVMError(Exception):
    pass


class ByteOp:
    def __init__(self, vm):
        self.vm = vm

    def binaryOperator(self, op):
        x, y = self.vm.popn(2)
        self.vm.push(BINARY_OPERATORS[op](x, y))

    def unaryOperator(self, op):
        x = self.vm.pop()
        self.vm.push(UNARY_OPERATORS[op](x))

    def LOAD_FAST(self, name):
        if name in self.vm.frame.f_locals:
            val = self.vm.frame.f_locals[name]
        else:
            raise UnboundLocalError(
                "local variable {} referenced before assignment".format(name)
            )
        self.vm.push(val)

    def RETURN_VALUE(self, arg):
        self.vm.return_value = self.vm.pop()
        return self.vm.return_value


class Frame(object):
    def __init__(
        self,
        f_code,
        f_globals,
        f_locals,
    ):
        self.f_code = f_code
        self.f_globals = f_globals
        self.f_locals = f_locals
        self.stack = []


# this is a very tiny stack machine interpreter used to interprete
# python bytecode, it's just for learning purpose.
# in paddlefx project, we are going to interprete the instructions
# into paddle python IR (fx graph), this IR can be used for
# further optimization or lowering to other backends.
class TinyVM:
    def __init__(self):
        self.frame = None
        self.return_value = None
        self.byteop = ByteOp(self)

    def push(self, *vals):
        self.frame.stack.extend(vals)

    def pop(self, i=0):
        return self.frame.stack.pop(-1 - i)

    def popn(self, n):
        if n:
            ret = self.frame.stack[-n:]
            self.frame.stack[-n:] = []
            return ret
        else:
            return []

    # if we are utilizing PEP523, there's no need to make frame from code object
    # the actual frame object will be passed from CPython, goes into eval_frame
    def make_frame(self, code, f_globals=None, f_locals=None):
        frame = Frame(
            f_code=code,
            f_globals=f_globals,
            f_locals=f_locals,
        )
        return frame

    def dispatch(self, instruction):
        """Dispatch by opname to the corresponding methods."""
        opname = instruction.opname

        if opname.startswith("UNARY_"):
            self.byteop.unaryOperator(opname[6:])
        elif opname.startswith("BINARY_"):
            self.byteop.binaryOperator(opname[7:])
        else:
            # dispatch
            if hasattr(self.byteop, opname):
                bytecode_fn = getattr(self.byteop, opname, None)
            else:
                raise TinyVMError("Unknown op: {} ".format(opname))
            bytecode_fn(instruction.argval)

    # This is the main entry point
    def run_code(self, code, f_globals=None, f_locals=None):
        """run code using f_globals and f_locals in our VM"""
        self.frame = self.make_frame(code, f_globals=f_globals, f_locals=f_locals)
        val = self.eval_frame(self.frame)
        return val

    def eval_frame(self, frame):
        instructions = dis.get_instructions(frame.f_code)
        for ins in instructions:
            print(ins)
            self.dispatch(ins)

        return self.return_value


if __name__ == "__main__":
    # Simplest of tests
    def add(a, b):
        return a + b

    a, b = 3, 4

    vm = TinyVM()
    # this should print 7, which is identical to CPython output
    print(vm.run_code(add.__code__, f_globals=globals(), f_locals=locals()))
