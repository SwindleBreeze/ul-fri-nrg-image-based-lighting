struct Uniforms {
  viewProj : mat4x4<f32>,
}

struct IrradianceParams {
  sampleDelta : f32,
  padding0 : f32,
  padding1 : f32,
  padding2 : f32,
}

@group(0) @binding(0) var<uniform> uniforms : Uniforms;
@group(0) @binding(1) var envMap : texture_cube<f32>;
@group(0) @binding(2) var envSampler : sampler;
@group(0) @binding(3) var<uniform> irradianceParams : IrradianceParams;

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

const PI : f32 = 3.14159265359;

@fragment
fn fs_main(@location(0) localPos : vec3<f32>) -> @location(0) vec4<f32> {
  let N = normalize(localPos);

  var up = vec3<f32>(0.0, 1.0, 0.0);
  let right = normalize(cross(up, N));
  up = normalize(cross(N, right));

  var irradiance = vec3<f32>(0.0);
  let sampleDelta : f32 = irradianceParams.sampleDelta;
  var nrSamples : f32 = 0.0;

  var phi : f32 = 0.0;
  loop {
    if (phi >= 2.0 * PI) { break; }
    var theta : f32 = 0.0;
    loop {
      if (theta >= 0.5 * PI) { break; }
      let tangentSample = vec3<f32>(
        sin(theta) * cos(phi),
        sin(theta) * sin(phi),
        cos(theta)
      );
      let sampleVec = tangentSample.x * right
                    + tangentSample.y * up
                    + tangentSample.z * N;
      irradiance += textureSample(envMap, envSampler, sampleVec).rgb
                  * cos(theta) * sin(theta);
      nrSamples += 1.0;
      theta += sampleDelta;
    }
    phi += sampleDelta;
  }

  irradiance = PI * irradiance / nrSamples;
  return vec4<f32>(irradiance, 1.0);
}
