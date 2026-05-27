struct CameraUniforms {
  viewProj : mat4x4<f32>,
  cameraPos : vec4<f32>,
}

struct MaterialUniforms {
  albedoMetallic : vec4<f32>,
  roughnessAoFlags : vec4<f32>,
}

struct IblParams {
  maxReflectionLod : f32,
  envYaw : f32,
  padding0 : f32,
  padding1 : f32,
}

@group(0) @binding(0) var<uniform> camera : CameraUniforms;
@group(0) @binding(1) var<uniform> iblParams : IblParams;

@group(1) @binding(0) var irradianceMap : texture_cube<f32>;
@group(1) @binding(1) var prefilterMap : texture_cube<f32>;
@group(1) @binding(2) var brdfLut : texture_2d<f32>;
@group(1) @binding(3) var iblSampler : sampler;
@group(1) @binding(4) var brdfSampler : sampler;
@group(2) @binding(0) var albedoMap : texture_2d<f32>;
@group(2) @binding(1) var metallicRoughnessMap : texture_2d<f32>;
@group(2) @binding(2) var normalMap : texture_2d<f32>;
@group(2) @binding(3) var materialSampler : sampler;
@group(2) @binding(4) var<uniform> material : MaterialUniforms;
@group(2) @binding(5) var<uniform> model : mat4x4<f32>;

struct VertexInput {
  @location(0) position : vec3<f32>,
  @location(1) normal : vec3<f32>,
  @location(2) uv : vec2<f32>,
  @location(3) tangent : vec4<f32>,
}

struct VertexOutput {
  @builtin(position) pos : vec4<f32>,
  @location(0) worldPos : vec3<f32>,
  @location(1) normal : vec3<f32>,
  @location(2) tangent : vec3<f32>,
  @location(3) bitangent : vec3<f32>,
  @location(4) uv : vec2<f32>,
}

fn rotateY(v : vec3<f32>, angle : f32) -> vec3<f32> {
  let c = cos(angle);
  let s = sin(angle);
  return vec3<f32>(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
}

@vertex
fn vs_main(input : VertexInput) -> VertexOutput {
  var out : VertexOutput;
  let worldPos4 = model * vec4<f32>(input.position, 1.0);
  out.worldPos = worldPos4.xyz;
  let normalMatrix = mat3x3<f32>(model[0].xyz, model[1].xyz, model[2].xyz);
  out.normal = normalize(normalMatrix * input.normal);
  out.tangent = normalize(normalMatrix * input.tangent.xyz);
  out.bitangent = normalize(cross(out.normal, out.tangent) * input.tangent.w);
  out.uv = input.uv;
  out.pos = camera.viewProj * worldPos4;
  return out;
}

fn FresnelSchlickRoughness(cosTheta : f32, F0 : vec3<f32>, roughness : f32) -> vec3<f32> {
  return F0 + (max(vec3<f32>(1.0 - roughness), F0) - F0)
            * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

@fragment
fn fs_main(input : VertexOutput) -> @location(0) vec4<f32> {
  let flags = material.roughnessAoFlags.z;
  let useAlbedoMap = (u32(flags) & 1u) != 0u;
  let useMrMap = (u32(flags) & 2u) != 0u;
  let useNormalMap = (u32(flags) & 4u) != 0u;

  var albedo = material.albedoMetallic.rgb;
  if (useAlbedoMap) {
    albedo *= textureSample(albedoMap, materialSampler, input.uv).rgb;
  }

  var metallic = material.albedoMetallic.a;
  var roughness = material.roughnessAoFlags.x;
  if (useMrMap) {
    let mr = textureSample(metallicRoughnessMap, materialSampler, input.uv);
    roughness *= mr.g;
    metallic *= mr.b;
  }

  let ao = material.roughnessAoFlags.y;
  roughness = clamp(roughness, 0.04, 1.0);

  var N = normalize(input.normal);
  if (useNormalMap) {
    let map = textureSample(normalMap, materialSampler, input.uv).rgb * 2.0 - 1.0;
    let TBN = mat3x3<f32>(normalize(input.tangent), normalize(input.bitangent), N);
    N = normalize(TBN * map);
  }

  let V = normalize(camera.cameraPos.xyz - input.worldPos);
  let R = reflect(-V, N);

  let envYaw = iblParams.envYaw;
  let Nenv = rotateY(N, envYaw);
  let Renv = rotateY(R, envYaw);

  let F0 = mix(vec3<f32>(0.04), albedo, metallic);
  let kS = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
  let kD = (vec3<f32>(1.0) - kS) * (1.0 - metallic);

  let irradiance = textureSample(irradianceMap, iblSampler, Nenv).rgb;
  let diffuse = irradiance * albedo;

  let lod = roughness * iblParams.maxReflectionLod;
  let prefilteredColor = textureSampleLevel(prefilterMap, iblSampler, Renv, lod).rgb;
  let NdotV = max(dot(N, V), 0.0);
  let brdf = textureSample(brdfLut, brdfSampler, vec2<f32>(NdotV, roughness)).rg;
  let specular = prefilteredColor * (F0 * brdf.x + brdf.y);

  var color = (kD * diffuse + specular) * ao;
  // Slight exposure so diffuse IBL is visible on rough dielectric / ground.
  color *= 1.35;
  color = color / (color + vec3<f32>(1.0));
  color = pow(color, vec3<f32>(1.0 / 2.2));
  return vec4<f32>(color, 1.0);
}
