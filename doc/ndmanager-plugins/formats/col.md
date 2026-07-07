# `.col.<method>.N` — collision decomposition results (YAML)

Written by `ndm_decomposecollisions`. One entry per candidate collision
spike. A **MethodSpecific** artifact under the
[variant naming convention](naming.md), resolved strictly as
`<base>.col.<method>.N`. The source `.clu` / `.res` / `.spk` files are not
modified.

```yaml
collisions:
  format: '1.0'
  spikeGroup: 1
  spikes:
    - spikeIndex: 4721
      isCollision: true
      components:
        - unit: 3
          shift: -2
          amplitude: 0.97
        - unit: 7
          shift: 8
          amplitude: 0.84
```


---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
