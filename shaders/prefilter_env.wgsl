struct Uniforms {
  viewProj : mat4x4<f32>,
}

struct PrefilterUniforms {
  roughness : f32,
  resolution : f32,
  sampleCount : f32,
  padding0 : f32,
}

@group(0) @binding(0) var<uniform> uniforms : Uniforms;
@group(0) @binding(1) var envMap : texture_cube<f32>;
@group(0) @binding(2) var envSampler : sampler;
@group(0) @binding(3) var<uniform> prefilter : PrefilterUniforms;

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

fn RadicalInverse_VdC(inputBits : u32) -> f32 {
  var bits = inputBits;
  bits = (bits << 16u) | (bits >> 16u);
  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
  return f32(bits) * 2.3283064365386963e-10;
}

fn Hammersley(i : u32, N : u32) -> vec2<f32> {
  return vec2<f32>(f32(i) / f32(N), RadicalInverse_VdC(i));
}

fn ImportanceSampleGGX(Xi : vec2<f32>, N : vec3<f32>, roughness : f32) -> vec3<f32> {
  let a = roughness * roughness;
  let phi = 2.0 * PI * Xi.x;
  let cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
  let sinTheta = sqrt(1.0 - cosTheta * cosTheta);
  let H = vec3<f32>(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

  var up : vec3<f32>;
  if abs(N.z) < 0.999 {
    up = vec3<f32>(0.0, 0.0, 1.0);
  } else {
    up = vec3<f32>(1.0, 0.0, 0.0);
  }
  let tangent = normalize(cross(up, N));
  let bitangent = cross(N, tangent);
  return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

@fragment
fn fs_main(@location(0) localPos : vec3<f32>) -> @location(0) vec4<f32> {
  let N = normalize(localPos);
  let R = N;
  let V = N;

  var prefilteredColor = vec3<f32>(0.0);
  var totalWeight : f32 = 0.0;
  let roughness = prefilter.roughness;

  let sampleCount = u32(prefilter.sampleCount);
  for (var i : u32 = 0u; i < sampleCount; i = i + 1u) {
    let Xi = Hammersley(i, sampleCount);
    let H = ImportanceSampleGGX(Xi, N, roughness);
    let L = normalize(2.0 * dot(V, H) * H - V);

    let NdotL = max(dot(N, L), 0.0);
    if (NdotL > 0.0) {
      let NdotH = max(dot(N, H), 0.0);
      let VdotH = max(dot(V, H), 0.0);
      let a = roughness * roughness;
      let a2 = a * a;
      let denom = (NdotH * NdotH * (a2 - 1.0) + 1.0);
      let D = a2 / (PI * denom * denom);
      let pdf = D * NdotH / (4.0 * VdotH) + 0.0001;

      let saTexel = 4.0 * PI / (6.0 * prefilter.resolution * prefilter.resolution);
      let saSample = 1.0 / (f32(sampleCount) * pdf + 0.0001);
      let mipLevel = select(0.5 * log2(saSample / saTexel), 0.0, roughness == 0.0);

      prefilteredColor += textureSampleLevel(envMap, envSampler, L, mipLevel).rgb * NdotL;
      totalWeight += NdotL;
    }
  }

  prefilteredColor = prefilteredColor / totalWeight;
  return vec4<f32>(prefilteredColor, 1.0);
}
