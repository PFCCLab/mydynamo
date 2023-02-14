import paddle
import operator
from typing import (Any,Callable)

# This is a proof of concept for a paddle IR. It is not intended to be a complete
# Lots of the codes are from torch.fx.
# Since Paddle doesnot have protocol like __torch_function__, 
# I haven to monkey patch paddle.add, paddle.nn.functional.relu
# to create a proxy for them.   

def _find_module(root, m):
    for n, p in root.named_children():
        if m is p:
            return n
    raise NameError('module is not installed as a submodule')

def print_nodes(nodes):
    from tabulate import tabulate
    node_specs = [[n.op, n.name, n.target, n.args, n.kwargs] for n in nodes]
    print(tabulate(node_specs, headers=['opcode', 'name', 'target', 'args', 'kwargs']))

# Store all nodes in a global list so we can print them later.
nodes = []

# Nodes represent a definition of a value in our graph of operators.
class Node:
    def __init__(self, name, op, target, args, kwargs):
        self.name = name  # unique name of value being created
        self.op = op  # the kind of operation = placeholder|call_method|call_module|call_function|getattr
        self.target = target  # for method/module/function, the name of the method/module/function/attr
        # being invoked, e.g add, layer1, or torch.add
        self.args = args
        self.kwargs = kwargs

    def __repr__(self):
        return self.name

def create_node(op, target=None, args=None, kwargs=None, name=None):
    assert op in ('call_function', 'call_method', 'get_param', 'call_module', 'placeholder')
    args = () if args is None else args
    kwargs = {} if kwargs is None else kwargs
    n = Node(name if name is not None else 'noname', op, target, args, kwargs)
    nodes.append(n)
    return n

def _create_proxy(op, target, args, kwargs, name=None):
    n = create_node(op, 
                     target, 
                     args, 
                     kwargs, 
                     name)
    return Proxy(n)

class Proxy:
    def __init__(self, node):
        self.node = node

    def __repr__(self):
        return f'Proxy({self.node.name})'

reflectable_magic_methods = {
    'add': '{} + {}',
    'sub': '{} - {}',
    'mul': '{} * {}',
    'floordiv': '{} // {}',
    'truediv': '{} / {}',
    'div': '{} / {}',
    'mod': '{} % {}',
    'pow': '{} ** {}',
    'lshift': '{} << {}',
    'rshift': '{} >> {}',
    'and': '{} & {}',
    'or': '{} | {}',
    'xor': '{} ^ {}',
    'getitem': '{}[{}]'
}

magic_methods = dict({
    'eq': '{} == {}',
    'ne': '{} != {}',
    'lt': '{} < {}',
    'gt': '{} > {}',
    'le': '{} <= {}',
    'ge': '{} >= {}',
    'pos': '+{}',
    'neg': '-{}',
    'invert': '~{}'}, **reflectable_magic_methods)

for method in magic_methods:
    def scope(method):
        def impl(*args, **kwargs):
            target = getattr(operator, method)
            return _create_proxy('call_function', target, args, kwargs, method)
        impl.__name__ = method
        as_magic = f'__{method}__'
        setattr(Proxy, as_magic, impl)
    scope(method)

def my_trace(root: Callable[..., Any]):
    fn = type(root).forward
    
    co = fn.__code__
    args = [root]

    names_iter = iter(co.co_varnames)

    next(names_iter)  # skip self
    for _ in range(1, co.co_argcount):       
        name = next(names_iter)   
        args.append(_create_proxy('placeholder', name, None, None, name))

    # monkey patch paddle.add to create a proxy for it
    orig_call = paddle.add
    def paddle_add_wrapper(*args, **kwargs):
        return _create_proxy('call_function', orig_call, args, kwargs, 'add')
    
    # monkey patch paddle.nn.functional.relu to create a proxy for it
    orig_relu_call = paddle.nn.functional.relu
    def paddle_relu_wrapper(*args, **kwargs):
        return _create_proxy('call_function', orig_relu_call, args, kwargs, 'relu')

    # monkey patch paddle.nn.Layer to create a proxy for it
    orig_module_call = paddle.nn.Layer.__call__
    def module_call_wrapper(mod, *args, **kwargs):
        target = _find_module(root, mod)
        return _create_proxy('call_module', target, args, kwargs, target)

    try:
        paddle.add = paddle_add_wrapper
        paddle.nn.functional.relu = paddle_relu_wrapper
        paddle.nn.Layer.__call__ = module_call_wrapper

        fn(*args)
    finally:
        paddle.add = orig_call
        paddle.nn.functional.relu = orig_relu_call 
        paddle.nn.Layer.__call__ = orig_module_call
    return

class MyNet(paddle.nn.Layer):
    def __init__(self):
        super().__init__()
        self._fc1 = paddle.nn.Linear(in_features=10, out_features=10)
        self._fc2 = paddle.nn.Linear(in_features=10, out_features=10)
        self._fc3 = paddle.nn.Linear(in_features=10, out_features=10)

    def forward(self, x):
        x = self._fc1(x)
        x = self._fc2(x)
        x = self._fc3(x)
        y = paddle.add(x=x, y=x)
        return paddle.nn.functional.relu(x=y)

net = MyNet()

nodes = []
# tracing a paddle layer
my_trace(net)
print_nodes(nodes)