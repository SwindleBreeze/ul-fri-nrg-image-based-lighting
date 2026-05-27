struct CameraUniforms {
  viewProj : mat4x4<f32>,
}

struct SkyParams {
  envYaw : f32,
  padding0 : f32,
  padding1 : f32,
  padding2 : f32,
}

@group(0) @binding(0) var<uniform> camera : CameraUniforms;
@group(0) @binding(1) var envMap : texture_cube<f32>;
@group(0) @binding(2) var envSampler : sampler;
@group(0) @binding(3) var<uniform> skyParams : SkyParams;

fn rotateY(v : vec3<f32>, angle : f32) -> vec3<f32> {
  let c = cos(angle);
  let s = sin(angle);
  return vec3<f32>(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
}

struct VertexInput {
  @location(0) position : vec3<f32>,
}

struct VertexOutput {
  @builtin(position) pos : vec4<f32>,
  @location(0) localPos : vec3<f32>,
}

@vertex
fn vs_main(input : VertexInput) -> VertexOutput {
  var out : VertexOutput;
  out.localPos = input.position;
  let worldPos = input.position * 100.0;
  let clip = camera.viewProj * vec4<f32>(worldPos, 1.0);
  out.pos = vec4<f32>(clip.xy, clip.w * 0.9999, clip.w);
  return out;
}

@fragment
fn fs_main(input : VertexOutput) -> @location(0) vec4<f32> {
  let dir = rotateY(normalize(input.localPos), skyParams.envYaw);
  let hdr = textureSample(envMap, envSampler, dir).rgb;
  let color = hdr / (hdr + vec3<f32>(1.0));
  return vec4<f32>(pow(color, vec3<f32>(1.0 / 2.2)), 1.0);
}
