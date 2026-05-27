struct Uniforms {
  viewProj : mat4x4<f32>,
}

@group(0) @binding(0) var<uniform> uniforms : Uniforms;
@group(0) @binding(1) var equirectMap : texture_2d<f32>;

struct VertexOutput {
  @builtin(position) pos : vec4<f32>,
  @location(0) localPos : vec3<f32>,
}

// Standard cube vertex positions (36 vertices, 12 triangles).
fn cubePositions() -> array<vec3<f32>, 36> {
  return array<vec3<f32>, 36>(
    // +X
    vec3<f32>( 1.0, -1.0, -1.0), vec3<f32>( 1.0, -1.0,  1.0), vec3<f32>( 1.0,  1.0,  1.0),
    vec3<f32>( 1.0, -1.0, -1.0), vec3<f32>( 1.0,  1.0,  1.0), vec3<f32>( 1.0,  1.0, -1.0),
    // -X
    vec3<f32>(-1.0, -1.0,  1.0), vec3<f32>(-1.0, -1.0, -1.0), vec3<f32>(-1.0,  1.0, -1.0),
    vec3<f32>(-1.0, -1.0,  1.0), vec3<f32>(-1.0,  1.0, -1.0), vec3<f32>(-1.0,  1.0,  1.0),
    // +Y
    vec3<f32>(-1.0,  1.0, -1.0), vec3<f32>( 1.0,  1.0, -1.0), vec3<f32>( 1.0,  1.0,  1.0),
    vec3<f32>(-1.0,  1.0, -1.0), vec3<f32>( 1.0,  1.0,  1.0), vec3<f32>(-1.0,  1.0,  1.0),
    // -Y
    vec3<f32>(-1.0, -1.0,  1.0), vec3<f32>( 1.0, -1.0,  1.0), vec3<f32>( 1.0, -1.0, -1.0),
    vec3<f32>(-1.0, -1.0,  1.0), vec3<f32>( 1.0, -1.0, -1.0), vec3<f32>(-1.0, -1.0, -1.0),
    // +Z
    vec3<f32>(-1.0, -1.0,  1.0), vec3<f32>(-1.0,  1.0,  1.0), vec3<f32>( 1.0,  1.0,  1.0),
    vec3<f32>(-1.0, -1.0,  1.0), vec3<f32>( 1.0,  1.0,  1.0), vec3<f32>( 1.0, -1.0,  1.0),
    // -Z
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

const invAtan = vec2<f32>(0.1591, 0.3183);

fn sampleSphericalMap(v : vec3<f32>) -> vec2<f32> {
  var uv = vec2<f32>(atan2(v.z, v.x), asin(clamp(v.y, -1.0, 1.0)));
  uv = uv * invAtan + vec2<f32>(0.5);
  uv.y = 1.0 - uv.y; // Match stbi_set_flip_vertically_on_load(true).
  return uv;
}

@fragment
fn fs_main(@location(0) localPos : vec3<f32>) -> @location(0) vec4<f32> {
  let uv = sampleSphericalMap(normalize(localPos));
  let dims = textureDimensions(equirectMap);
  let coord = vec2<i32>(uv * vec2<f32>(dims));
  let maxCoord = vec2<i32>(dims) - vec2<i32>(1);
  let texel = clamp(coord, vec2<i32>(0), maxCoord);
  let color = textureLoad(equirectMap, texel, 0);
  return vec4<f32>(color.rgb, 1.0);
}
