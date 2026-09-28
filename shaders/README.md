# jydRenderer shader language

Shader files use the `.jydshader` extension and contain one `vertex` stage and
one `fragment` stage:

```text
shader MyShader {
    vertex {
        position = transform_point(mvp, in.position);
        normal = normalize(transform_direction(vp, in.normal));
        texcoord = in.texcoord;
    }

    fragment {
        let texel = sample(in.texcoord);
        color = texel;
    }
}
```

## Inputs and uniforms

- Vertex inputs: `in.position` (`vec3`), `in.normal` (`vec3`),
  `in.texcoord` (`vec2`).
- Fragment inputs: `in.position` (`vec4`), `in.normal` (`vec3`),
  `in.texcoord` (`vec2`).
- Uniforms: `mvp`, `vp`, `cameraPosition`, `specularLightDirection`,
  `diffuseLightDirection`, `ambientLightColor`.

The vertex stage must assign `position`, `normal`, and `texcoord`. The fragment
stage must assign `color` (`vec4`, normalized RGBA). It may assign scalar
`discard`; zero keeps the fragment and a non-zero value discards it.

## Expressions

The first version supports `+`, `-`, `*`, `/`, parentheses, `let` variables,
scalar/vector arithmetic, and these functions:

- `vec2`, `vec3`, `vec4`
- `transform_point(mat4, vec3)`
- `transform_direction(mat4, vec3)`
- `normalize(vector)`
- `dot(vector, vector)`
- `clamp(value, minimum, maximum)`
- `sample(texcoord)`

The compiler reports syntax errors with line and column information. Type and
output errors identify the vertex or fragment stage. The source is compiled to
LLVM IR and ORC JIT machine code once when rendering starts.
