@group(0) @binding(0) var outputLUT : texture_storage_2d<rg32float, write>;

struct BrdfUniforms {
  sampleCount : f32,
  padding0 : f32,
  padding1 : f32,
  padding2 : f32,
}

@group(0) @binding(1) var<uniform> brdfParams : BrdfUniforms;

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

fn GeometrySchlickGGX_IBL(NdotV : f32, roughness : f32) -> f32 {
  let a = roughness;
  let k = (a * a) / 2.0;
  return NdotV / (NdotV * (1.0 - k) + k);
}

fn GeometrySmith(NdotV : f32, NdotL : f32, roughness : f32) -> f32 {
  return GeometrySchlickGGX_IBL(NdotV, roughness)
       * GeometrySchlickGGX_IBL(NdotL, roughness);
}

@compute @workgroup_size(16, 16, 1)
fn cs_main(@builtin(global_invocation_id) id : vec3<u32>) {
  let size = textureDimensions(outputLUT);
  if (id.x >= size.x || id.y >= size.y) { return; }

  let NdotV = (f32(id.x) + 0.5) / f32(size.x);
  let roughness = (f32(id.y) + 0.5) / f32(size.y);

  let V = vec3<f32>(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);
  let N = vec3<f32>(0.0, 0.0, 1.0);

  var A : f32 = 0.0;
  var B : f32 = 0.0;

  let sampleCount = u32(brdfParams.sampleCount);
  for (var i : u32 = 0u; i < sampleCount; i = i + 1u) {
    let Xi = Hammersley(i, sampleCount);
    let H = ImportanceSampleGGX(Xi, N, roughness);
    let L = normalize(2.0 * dot(V, H) * H - V);

    let NdotL = max(L.z, 0.0);
    let NdotH = max(H.z, 0.0);
    let VdotH = max(dot(V, H), 0.0);

    if (NdotL > 0.0) {
      let G = GeometrySmith(NdotV, NdotL, roughness);
      let G_Vis = (G * VdotH) / (NdotH * NdotV);
      let Fc = pow(1.0 - VdotH, 5.0);
      A += (1.0 - Fc) * G_Vis;
      B += Fc * G_Vis;
    }
  }

  let result = vec2<f32>(A, B) / f32(sampleCount);
  textureStore(outputLUT, vec2<i32>(id.xy), vec4<f32>(result, 0.0, 1.0));
}
