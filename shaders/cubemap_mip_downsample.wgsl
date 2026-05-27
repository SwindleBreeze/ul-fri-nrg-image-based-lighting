struct Uniforms {
  viewProj : mat4x4<f32>,
}

struct MipUniforms {
  sourceMip : f32,
  padding0 : f32,
  padding1 : f32,
  padding2 : f32,
}

@group(0) @binding(0) var<uniform> uniforms : Uniforms;
@group(0) @binding(1) var envMap : texture_cube<f32>;
@group(0) @binding(2) var envSampler : sampler;
@group(0) @binding(3) var<uniform> mipUniforms : MipUniforms;

struct VertexOutput {
  @builtin(position) pos : vec4<f32>,
  @location(0) localPos : vec3<f32>,
}

fn cubePositions() -> array<vec3<f32>, 36> {
  return array<vec3<f32>, 36>(
    vec3<f32>( 1.0, -1.0, -1.0), vec3<f32>( 1.0, -1.0,  1.0), vec3<f32>( 1.0,  1.0,  1.0),
    vec3<f32>( 1.0, -1.0, -1.0), vec3<f32>( 1.0,  1.0,  1.0), vec3<f32>( 1.0,  1.0, -1.0),
    vec3<f32>(-1.0, -1.0,  1.0), vec3<f32>(-1.0, -1.0, -1.0), vec3<f32>(-1.0,  1.0, -1.0),
    vec3<f32>(-1.0, -1.0,  1.0), vec3<f32>(-1.0,  1.0, -1.0), vec3<f32>(-1.0,  1.0,  1.0),
    vec3<f32>(-1.0,  1.0, -1.0), vec3<f32>( 1.0,  1.0, -1.0), vec3<f32>( 1.0,  1.0,  1.0),
    vec3<f32>(-1.0,  1.0, -1.0), vec3<f32>( 1.0,  1.0,  1.0), vec3<f32>(-1.0,  1.0,  1.0),
    vec3<f32>(-1.0, -1.0,  1.0), vec3<f32>( 1.0, -1.0,  1.0), vec3<f32>( 1.0, -1.0, -1.0),
    vec3<f32>(-1.0, -1.0,  1.0), vec3<f32>( 1.0, -1.0, -1.0), vec3<f32>(-1.0, -1.0, -1.0),
    vec3<f32>(-1.0, -1.0,  1.0), vec3<f32>(-1.0,  1.0,  1.0), vec3<f32>( 1.0,  1.0,  1.0),
    vec3<f32>(-1.0, -1.0,  1.0), vec3<f32>( 1.0,  1.0,  1.0), vec3<f32>( 1.0, -1.0,  1.0),
    vec3<f32>( 1.0, -1.0, -1.0), vec3<f32>( 1.0,  1.0, -1.0), vec3<f32>(-1.0,  1.0, -1.0),
    vec3<f32>( 1.0, -1.0, -1.0), vec3<f32>(-1.0,  1.0, -1.0), vec3<f32>(-1.0, -1.0, -1.0)
  );
}

@vertex
fn vs_main(@builtin(vertex_index) vIdx : u32) -> VertexOutput {
  let positions = cubePositions();
  let local = positions[vIdx];

  var out : VertexOutput;
  out.localPos = local;
  out.pos = uniforms.viewProj * vec4<f32>(local, 1.0);
  return out;
}

@fragment
fn fs_main(@location(0) localPos : vec3<f32>) -> @location(0) vec4<f32> {
  let dir = normalize(localPos);
  let color = textureSampleLevel(envMap, envSampler, dir, mipUniforms.sourceMip);
  return vec4<f32>(color.rgb, 1.0);
}
