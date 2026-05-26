for fn in ['src/gpu/shader/SpirvEmitter.h', 'src/gpu/shader/SpirvEmitter.cpp']:
    with open(fn, 'r') as f:
        c = f.read()
    c = c.replace('std::vector<u32>&', 'std::span<const u32>')
    with open(fn, 'w') as f:
        f.write(c)
    print(f'Reverted {fn}')
