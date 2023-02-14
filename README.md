# My Dynamo

Learning internals of torchdynamo

- [torchdynamo](https://github.com/pytorch/torchdynamo)
- [PEP 523](https://www.python.org/dev/peps/pep-0523/)

## Development Setup

initial setup
```
python setup.py develop  # compiles C/C++ extension
python naive_transform.py
```

## Notes

- naive_transform.py is demostrating howto transform bytecodes of `a + b` to `a * b + a + b`
- paddle_fx_poc.py is a poc for creating a python IR for paddle python program.

## Random thoughts

也许可以试着先做一个玩具项目，来试试把些技术引入到Paddle。

关于 python IR for paddle python program (a.k.a paddle.fx)
- 实现完整的Graph/Node数据结构，从而可以表示出来一个python IR。
- 实现一个 GraphLayer（类似torch.fx.GraphModule），是从`paddle.nn.Layer`派生出来的，用来作为trace后得到的结果。
- 实现完整的trace功能，这需要能自动的对所有paddle.* 与 paddle.nn.functional.* 的API做monkey patch。
- 一系列对Graph进行操作的工具，实现支持对Graph进行操作（添加、删除、编辑Node）；生成python code等。
- Test Cases：选取10个左右的paddle的模型，对比trace前与trace后的运行的结果，两次结果需要相同。
- Simple Use Cases
    - 方便的提取模型中间的特征表示；
    - 修改trace后的Graph，得到新的GraphLayer；
    - 其他？
- Advanced UseCases：
    - Lower to backends：将paddle python IR lower到另外的后端并运行，比如转换到TRT或者ONNX Runtime上运行。
    - 把paddle python IR转换成torch.fx IR，并运行。（或者相反的方向）

关于类torchdynamo引入到paddle

虽然感觉上是可行的，但还是有不少的技术需要先吃透。需要先能有一个POC出来。


